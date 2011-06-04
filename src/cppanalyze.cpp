#include "rename_consumer.h"

#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/CompilerInstance.h"

using namespace clang;

namespace {


class CppAnalyze : public PluginASTAction
{
public:
    CppAnalyze():
        m_arg_src_root_dir("tests")
    {}

protected:
    virtual ASTConsumer* CreateASTConsumer(CompilerInstance& compiler, llvm::StringRef)
    {
        return new RenameConsumer(compiler, m_arg_src_root_dir);
    }

    virtual bool ParseArgs(const CompilerInstance&,
                           const std::vector<std::string>& args)
    {
        if (args.empty())
            return true;

        assert(args.size() == 1); // for now handle only one argument
        m_arg_src_root_dir = args[0];
        return true;
    }

private:
    std::string m_arg_src_root_dir;
};


}

static FrontendPluginRegistry::Add<CppAnalyze>
X("rename", "rename code according to naming style conventions");
