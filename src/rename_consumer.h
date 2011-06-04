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
        loc = m_source_manager.getInstantiationLoc(loc);
        if (loc.isInvalid()) return true;

        // ignore stuff from system headers
        if (m_source_manager.isInSystemHeader(loc)) return true;


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

    // store new names of given named declarations
    typedef std::map<NamedDecl*, std::string> RenameMapType;
    RenameMapType m_renamed_decls;

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
    // tools to save changes
    //===--------------------------------------------------------------------===//

    void rewrite_if_found(NamedDecl* const d, const SourceLocation& loc)
    {
        RenameMapType::const_iterator decl_it = m_renamed_decls.find(d);

        if (decl_it != m_renamed_decls.end())
            write(d, decl_it->second, loc);
    }

    void change_and_rewrite(NamedDecl* const d, const std::string& new_name,
                            const SourceLocation& loc)
    {
        m_renamed_decls[d] = new_name;
        write(d, new_name, loc);
    }

    void write(NamedDecl* const d, const std::string& new_name,
               const SourceLocation& loc)
    {
        m_rewriter.ReplaceText(loc,
                               d->getNameAsString().length(),
                               new_name);
    }

    //===--------------------------------------------------------------------===//
    // RecursiveASTVisitor implicit interface
    //===--------------------------------------------------------------------===//

    /// Visit statements

    bool VisitCallExpr(CallExpr *Node)
    {
        if (shouldIgnoreLoc(Node->getExprLoc())) return true;

        if (Node->getDirectCallee())
        {
            FunctionDecl *func_decl = Node->getDirectCallee();
            rewrite_if_found(func_decl, Node->getLocStart());
        }
        return true;
    }

    bool VisitMemberExpr(MemberExpr *Node)
    {
        if (shouldIgnoreLoc(Node->getExprLoc())) return true;

        // TODO handle methods
        if (isa<CXXMethodDecl>(Node->getMemberDecl())) return true;

        FieldDecl *member_decl = dyn_cast<FieldDecl>(Node->getMemberDecl()); // ValueDecl : FieldDecl or CXXMethodDecl
        assert(member_decl);

        if (FieldDecl *template_parent_field_decl = getInstantiatedFrom(member_decl))
            member_decl = template_parent_field_decl;

        assert(member_decl); // we handle only c++ code
        rewrite_if_found(member_decl, Node->getMemberLoc());
        return true;
    }


    /// Visit decls

    bool VisitFunctionDecl(FunctionDecl* fun_decl)
    {
        // if (shouldIgnoreLoc(fun_decl->getLocation())) return true;

        // // ignore main function and methods
        // if (!fun_decl->isMain() && !isa<CXXMethodDecl>(fun_decl))
        // {
        //     std::stringstream ss;
        //     ss << fun_decl->getNameAsString() << "-renamed";
        //     change_and_rewrite(fun_decl, ss.str(), fun_decl->getNameInfo().getLoc());
        // }

        return true;
    }

    bool VisitFieldDecl(FieldDecl* field_decl)
    {
        if (shouldIgnoreLoc(field_decl->getLocation())) return true;

        std::string new_field_name;
        if (renameField(field_decl->getNameAsString(), new_field_name))
        {
            emitWarning(field_decl->getLocation(), "wrong name for field");
            change_and_rewrite(field_decl, new_field_name, field_decl->getLocation());
        }

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

            std::string new_field_name;
            if (renameField(field_decl->getNameAsString(), new_field_name))
            {
                change_and_rewrite(field_decl, new_field_name, (*i)->getMemberLocation());
            }
        }

        return true;
    }


    bool renameField(const std::string& field_name, std::string& new_name) const
    {
        const llvm::StringRef llvm_name(field_name);
        if (!llvm_name.startswith("m_"))
        {
            std::stringstream ss;
            if (llvm_name.startswith("_"))
                ss << "m";
            else
                ss << "m_";
            ss << field_name;
            new_name = ss.str();
            return true;
        }

        return false;
    }
};

#endif // RENAME_CONSUMER_H
