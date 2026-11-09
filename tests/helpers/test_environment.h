#ifndef TEST_ENVIRONMENT_H
#define TEST_ENVIRONMENT_H

#include <core/configmgr2.h>
#include <core/sessionconfig.h>

namespace tests {

struct TestEnvironment {
  bool m_markdownExperiencePromptEnabled = false;

  void apply(vnotex::ConfigMgr2 &p_config) const {
    p_config.getSessionConfig().setHasConfirmedMarkdownEditorConcealment(
        !m_markdownExperiencePromptEnabled);
  }
};

} // namespace tests

#endif // TEST_ENVIRONMENT_H
