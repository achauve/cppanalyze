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
    // renaming/rewriting helpers
    //===--------------------------------------------------------------------===//

    std::string renameDecl(NamedDecl * const decl)
    {
        assert(decl);

        RenameMapType::const_iterator decl_it = m_renamed_decls.find(decl);
        if (decl_it != m_renamed_decls.end())
            return decl_it->second;

        std::string decl_name = decl->getNameAsString();
        const llvm::StringRef llvm_name(decl_name);
        if (!llvm_name.startswith("m_"))
        {
            emitWarning(decl->getLocation(), "wrong name");

            std::stringstream ss;
            if (llvm_name.startswith("_"))
                ss << "m";
            else
                ss << "m_";
            ss << decl_name;
            decl_name = ss.str();

            m_renamed_decls[decl] = decl_name;
        }

        return decl_name;
    }

    void rewriteDecl(NamedDecl * const d, const SourceLocation& loc)
    {
        const std::string decl_name = d->getNameAsString();
        const std::string new_name = renameDecl(d);

        if (decl_name != new_name)
            m_rewriter.ReplaceText(loc,
                                   decl_name.length(),
                                   new_name);
    }

    //===--------------------------------------------------------------------===//
    // RecursiveASTVisitor implicit interface
    //===--------------------------------------------------------------------===//

    /// Visit statements

    bool VisitCallExpr(CallExpr *Node)
    {
        // if (shouldIgnoreLoc(Node->getExprLoc())) return true;

        // if (Node->getDirectCallee())
        // {
        //     FunctionDecl *func_decl = Node->getDirectCallee();
        //     rewriteDecl(func_decl, Node->getLocStart());
        // }
        return true;
    }

    bool VisitMemberExpr(MemberExpr *Node)
    {
        if (shouldIgnoreLoc(Node->getExprLoc())) return true;

        // TODO handle methods
        if (isa<CXXMethodDecl>(Node->getMemberDecl())) return true;

        FieldDecl *member_decl = dyn_cast<FieldDecl>(Node->getMemberDecl()); // ValueDecl : FieldDecl or CXXMethodDecl
        assert(member_decl);

        assert(member_decl); // we handle only c++ code
        rewriteDecl(member_decl, Node->getMemberLoc());
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

        rewriteDecl(field_decl, field_decl->getLocation());
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

            rewriteDecl(field_decl, (*i)->getMemberLocation());
        }

        return true;
    }

};

#endif // RENAME_CONSUMER_H
