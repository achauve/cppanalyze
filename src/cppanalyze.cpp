#include "rename_consumer.h"

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/CompilerInstance.h"

using namespace clang;

namespace {


class CppAnalyze : public PluginASTAction
{
protected:
    virtual ASTConsumer* CreateASTConsumer(CompilerInstance& compiler, llvm::StringRef)
    {
        return new RenameConsumer(compiler);
    }

    virtual bool ParseArgs(const CompilerInstance&,
                           const std::vector<std::string>& args)
    {
        /// not useful for now
        return true;
    }

};


}

static FrontendPluginRegistry::Add<CppAnalyze>
X("rename", "rename code according to naming style conventions");
