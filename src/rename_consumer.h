#ifndef RENAME_CONSUMER_H
#define RENAME_CONSUMER_H

#include <sstream>
#include <string>
#include <fstream>

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Frontend/CompilerInstance.h"

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceLocation.h" // FileID
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h" // FileEntry

#include "clang/Rewrite/Rewriter.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "boost/filesystem.hpp"

using namespace clang;


/// getInstantiatedFrom - if the parent of this (FieldDecl* d) is an
/// instanciation of a template class, return the corresponding FieldDecl* of this
/// template class.
FieldDecl* getInstantiatedFrom(FieldDecl const * const d)
{
    ClassTemplateSpecializationDecl const * const parent =
        dyn_cast<ClassTemplateSpecializationDecl>(d->getParent());

    // check the parent of this field is an instanciation of a class template
    if (!parent || parent->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
        return 0;

    CXXRecordDecl const * const generic_parent =
        parent->getSpecializedTemplate()->getTemplatedDecl();
    assert(generic_parent);

    for(CXXRecordDecl::field_iterator f=generic_parent->field_begin(),
        e=generic_parent->field_end(); f != e; ++f)
    {
        if ((*f)->getNameAsString() == d->getNameAsString())
            return *f;
    }

    // should never get here
    return 0;
}


class CommonASTConsumer : public ASTConsumer
{
protected:
    CommonASTConsumer(CompilerInstance& compiler, const std::string src_root_dir):
        m_compiler(compiler),
        m_source_manager(compiler.getSourceManager()),
        m_src_root_dir(src_root_dir)
    {}


    void emitWarning(const SourceLocation& loc, const std::string& message)
    {
        assert(m_compiler.hasDiagnostics());
        Diagnostic& diagnostics = m_compiler.getDiagnostics();
        const FullSourceLoc full(loc, m_compiler.getSourceManager());
        const unsigned id = diagnostics.getCustomDiagID(Diagnostic::Warning, message);
        diagnostics.Report(full, id);
    }

    bool shouldIgnoreLoc(SourceLocation loc)
    {
        // get the location where the characters are actually written
        // usefull for macros
        loc = m_source_manager.getSpellingLoc(loc);

        // then ignore loc that are either:
        // - invalid
        // - from system headers
        // - not inside the specified source tree

        if (loc.isInvalid() || m_source_manager.isInSystemHeader(loc))
            return true;

        const FullSourceLoc full_loc(loc, m_source_manager);
        const FileEntry* const file_entry =
            m_source_manager.getFileEntryForID(full_loc.getFileID());

        bool result = full_loc.getFileID() == m_source_manager.getMainFileID();
        if (!result && file_entry)
        {
            const std::string dir(file_entry->getDir()->getName());
            result = llvm::StringRef(dir).find(m_src_root_dir) != llvm::StringRef::npos ;
        }

        if (result)
            m_traversed_file_ids.push_back(full_loc.getFileID());

        return !result;
    }

    CompilerInstance& m_compiler;
    SourceManager& m_source_manager;
    std::string m_src_root_dir;
    std::vector<FileID> m_traversed_file_ids;
};


class RenameConsumer : public CommonASTConsumer, public RecursiveASTVisitor<RenameConsumer>
{
    Rewriter m_rewriter;

public:
    RenameConsumer(CompilerInstance& compiler, const std::string src_root_dir):
        CommonASTConsumer(compiler, src_root_dir),
        m_rewriter(m_source_manager, compiler.getLangOpts())
    {}


    void rewriteFiles()
    {
        namespace fs = boost::filesystem;

        std::sort(m_traversed_file_ids.begin(), m_traversed_file_ids.end());
        std::vector<FileID>::iterator new_end = std::unique(m_traversed_file_ids.begin(),
                                                            m_traversed_file_ids.end());

        for (std::vector<FileID>::const_iterator id = m_traversed_file_ids.begin();
             id != new_end; ++id)
        {
            const FileEntry& file_entry = *m_source_manager.getFileEntryForID(*id);

            const fs::path file_path(file_entry.getName());
            const fs::path renamed_path = "cppanalyze-renamed" / file_path.parent_path();
            const fs::path renamed_file_path = renamed_path / file_path.filename();

            fs::create_directories(renamed_path);

            // Get the buffer corresponding to the current FileID.
            // If we haven't changed it, then we are done.
            if (const RewriteBuffer* rewriter_buffer =
                m_rewriter.getRewriteBufferFor(*id))
            {
                llvm::outs() << "--------------------\nSrc file changed: " << file_entry.getName() << "\n";
                llvm::outs() << "===> Rewriting file: " << renamed_file_path.string() << "\n";
                std::ofstream out(renamed_file_path.c_str(), std::ios::out | std::ios::trunc);
                assert(out.good());
                // XXX do not rewrite file if not neccessary; if file already
                // exists, assert there is no diff
                out << std::string(rewriter_buffer->begin(), rewriter_buffer->end());
            }
            else
                llvm::outs() << "--------------------\nNo changes in " << file_entry.getName() << "\n";
        }

    }

    //===--------------------------------------------------------------------===//
    // ASTConsumer virtual interface
    //===--------------------------------------------------------------------===//

    virtual void HandleTranslationUnit(ASTContext &context)
    {
        // traverse AST to visit declarations and statements, and rename them
        // if needed
        TranslationUnitDecl* tu_decl = context.getTranslationUnitDecl();
        TraverseDecl(tu_decl);

        rewriteFiles();
    }


    //===--------------------------------------------------------------------===//
    // renaming/rewriting helpers
    //===--------------------------------------------------------------------===//

    std::string rename(const std::string& name)
    {
        const llvm::StringRef llvm_name(name);
        if (llvm_name.startswith("m_"))
            return name;

        std::stringstream ss;
        if (llvm_name.startswith("_"))
            ss << "m";
        else
            ss << "m_";
        ss << name;
        return ss.str();
    }

    void rewrite(const std::string& name, const SourceLocation& loc)
    {
        // XXX too much warning
        emitWarning(loc, "wrong name");
        const SourceLocation rewrite_loc = m_source_manager.getSpellingLoc(loc);
        const std::string new_name = rename(name);

        if (new_name != name)
            m_rewriter.ReplaceText(rewrite_loc,
                                   name.length(),
                                   new_name);
    }

    /// useful for debug
    void printLoc(const SourceLocation& loc, const std::string& msg="") const
    {
        const FullSourceLoc inst_full_loc(m_source_manager.getInstantiationLoc(loc), m_source_manager);
        const FullSourceLoc spell_full_loc(m_source_manager.getSpellingLoc(loc), m_source_manager);
        const FileEntry* inst_file_entry = m_source_manager.getFileEntryForID(inst_full_loc.getFileID());
        const FileEntry* spell_file_entry = m_source_manager.getFileEntryForID(spell_full_loc.getFileID());

        llvm::outs() << "---- " << msg << " loc:";

        llvm::outs()<<"\n\t\t";
        if (!inst_file_entry)
            llvm::outs() << "No file entry for instanciation source location\n";
        else
            llvm::outs() << " ; Inst file=" << inst_file_entry->getName()
                     << " ; Inst col=" << inst_full_loc.getInstantiationColumnNumber() << " line=" << inst_full_loc.getInstantiationLineNumber();

        llvm::outs()<<"\n\t\t";
        if (!spell_file_entry)
            llvm::outs() << "No file entry for spelling source location\n";
        else
            llvm::outs() << " ; Spell file=" << spell_file_entry->getName()
                     << " ; Spell col=" << spell_full_loc.getSpellingColumnNumber() << " line=" << spell_full_loc.getSpellingLineNumber()
                     << "\n";
    }

    //===--------------------------------------------------------------------===//
    // RecursiveASTVisitor implicit interface
    //===--------------------------------------------------------------------===//

    /// Visit statements

    bool VisitMemberExpr(MemberExpr *Node)
    {
        if (shouldIgnoreLoc(Node->getExprLoc())) return true;

        // TODO handle methods
        if (isa<CXXMethodDecl>(Node->getMemberDecl())) return true;

        FieldDecl *member_decl = dyn_cast<FieldDecl>(Node->getMemberDecl()); // ValueDecl : FieldDecl or CXXMethodDecl
        assert(member_decl);

        if (FieldDecl *template_parent_field_decl = getInstantiatedFrom(member_decl))
            member_decl = template_parent_field_decl;
        assert(member_decl);

        if (shouldIgnoreLoc(member_decl->getLocation())) return true;

        // debug: printLoc(Node->getMemberLoc(), member_decl->getNameAsString());
        rewrite(member_decl->getNameAsString(), Node->getMemberLoc());
        return true;
    }


    /// Visit decls

    bool VisitCXXRecordDecl(CXXRecordDecl* class_decl)
    {
        if (shouldIgnoreLoc(class_decl->getLocation())) return true;

        // Skip declarations
        if (!class_decl->isThisDeclarationADefinition()) return true;

        // Skip non template
        ClassTemplateDecl * const template_class_decl =
            class_decl->getDescribedClassTemplate();
        if (!template_class_decl) return true;

        llvm::outs() << "Visiting template class: " << class_decl->getName() << "\n";


        // Walk through specializations (XXX not partial for now)
        for (ClassTemplateDecl::spec_iterator spec_decl_it = template_class_decl->spec_begin(),
             spec_decl_end = template_class_decl->spec_end();
             spec_decl_it != spec_decl_end; ++spec_decl_it)
        {
            ClassTemplateSpecializationDecl * const spec_decl = *spec_decl_it;
            llvm::outs() << "\t specialization: " << spec_decl->getName() << "\n";

            // walk throug methods
            for (CXXRecordDecl::method_iterator method_it = spec_decl->method_begin(),
                 method_end = spec_decl->method_end();
                 method_it != method_end; ++method_it)
            {
                CXXMethodDecl* const method_decl = *method_it;

                // skip compiler auto generated methods
                if (!method_decl->isUserProvided()) continue;

                TraverseDecl(method_decl);

                llvm::outs() << "\t method: " << method_decl->getName() << "\n";
            }
        }

        llvm::outs().flush();
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* fun_decl)
    {
        if (shouldIgnoreLoc(fun_decl->getLocation())) return true;

        // skip declarations
        if (!fun_decl->isThisDeclarationADefinition()) return true;

        // Skip non template
        FunctionTemplateDecl* const template_fun_decl =
                fun_decl->getDescribedFunctionTemplate();
        if (!template_fun_decl) return true;

        llvm::outs() << "Visiting template function: " << fun_decl->getName() << "\n";

        // Walk through specializations (XXX not partial for now)
        for (FunctionTemplateDecl::spec_iterator spec_decl_it = template_fun_decl->spec_begin(),
             spec_decl_end = template_fun_decl->spec_end();
             spec_decl_it != spec_decl_end; ++spec_decl_it)
        {
            FunctionDecl* const spec_decl = *spec_decl_it;
            llvm::outs() << "\t specialization: " << spec_decl->getName() << "\n";

            TraverseDecl(spec_decl);
        }

        llvm::outs().flush();
        return true;
    }

    bool VisitFieldDecl(FieldDecl* field_decl)
    {
        if (shouldIgnoreLoc(field_decl->getLocation())) return true;

        rewrite(field_decl->getNameAsString(), field_decl->getLocation());
        return true;
    }


    bool VisitCXXConstructorDecl(CXXConstructorDecl* constructor_decl)
    {
        if (shouldIgnoreLoc(constructor_decl->getLocation())) return true;

        for (CXXConstructorDecl::init_iterator i=constructor_decl->init_begin(),
             e=constructor_decl->init_end();
             i != e;
             ++i)
        {
            // skip non meber initializers (base class initializers)
            if (!(*i)->isMemberInitializer())
                continue;
            // skip non-explicitly-written calls to default member initializer/constructor
            if (!(*i)->isWritten())
                continue;

            FieldDecl* field_decl = (*i)->getMember();
            assert(field_decl);

            rewrite(field_decl->getNameAsString(), (*i)->getMemberLocation());
        }

        return true;
    }


};

#endif // RENAME_CONSUMER_H
