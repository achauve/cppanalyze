#ifndef RENAME_CONSUMER_H
#define RENAME_CONSUMER_H

#include <sstream>
#include <string>

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
    CommonASTConsumer(CompilerInstance& compiler):
        m_compiler(compiler),
        m_source_manager(compiler.getSourceManager()),
        m_src_root_dir("tests")
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
    llvm::raw_ostream* m_out_file;

    // store new names of given named declarations
    typedef std::map<NamedDecl*, std::string> RenameMapType;
    RenameMapType m_renamed_decls;

public:
    RenameConsumer(CompilerInstance& compiler):
        CommonASTConsumer(compiler),
        m_rewriter(m_source_manager, compiler.getLangOpts()),
        m_out_file(&llvm::errs())
    {}


    void rewriteFiles()
    {
        std::sort(m_traversed_file_ids.begin(), m_traversed_file_ids.end());
        std::vector<FileID>::iterator new_end = std::unique(m_traversed_file_ids.begin(),
                                                            m_traversed_file_ids.end());

        for (std::vector<FileID>::const_iterator id = m_traversed_file_ids.begin();
             id != new_end; ++id)
        {
            const FileEntry& file_entry = *m_source_manager.getFileEntryForID(*id);

            // Get the buffer corresponding to the current FileID.
            // If we haven't changed it, then we are done.
            if (const RewriteBuffer* rewriter_buffer =
                m_rewriter.getRewriteBufferFor(*id))
            {
                llvm::outs() << "--------------------\nSrc file changed: " << file_entry.getName() << "\n";
                *m_out_file << std::string(rewriter_buffer->begin(), rewriter_buffer->end());
            }
            else
            {
                llvm::errs() << "--------------------\nNo changes in " << file_entry.getName() << "\n";
            }

            m_out_file->flush();

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
        if (shouldIgnoreLoc(fun_decl->getLocation())) return true;

        // ignore main function and methods
        if (!fun_decl->isMain() && !isa<CXXMethodDecl>(fun_decl))
        {
            std::stringstream ss;
            ss << fun_decl->getNameAsString() << "-renamed";
            change_and_rewrite(fun_decl, ss.str(), fun_decl->getNameInfo().getLoc());
        }

        return true;
    }

    bool VisitFieldDecl(FieldDecl* field_decl)
    {
        if (shouldIgnoreLoc(field_decl->getLocation())) return true;

        const std::string name = field_decl->getNameAsString();
        const llvm::StringRef llvm_name(name);
        if (!llvm_name.startswith("m_"))
        {
            emitWarning(field_decl->getLocation(), "wrong name for field");

            std::stringstream ss;
            if (llvm_name.startswith("_"))
                ss << "m";
            else
                ss << "m_";
            ss << name;

            change_and_rewrite(field_decl, ss.str(), field_decl->getLocation());
        }

        return true;
    }
};

#endif // RENAME_CONSUMER_H
