#include "test_common.h"
#include "../pw_helper.cpp"
#include <unistd.h>

static void test_config_parser() {
    char path[] = "/tmp/pipewine-config-test-XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0, "Temporary config created");
    if (fd < 0) return;
    close(fd);
    pw_helper_init_args args;
    pw_asio_init_default_config(&args);
    auto write = [&](const char *content) { std::ofstream out(path); out << content; };
    write("\n \t\n# comment\n[audio]\nsample_rate = 48000\nbuffer_size = 128\ninput_channels = 4\noutput_channels = 4\n[devices]\ninput_device = \noutput_device = test\n");
    TEST_ASSERT_EQ(0, pw_asio_load_config(&args, path), "Blank lines and empty device names parse safely");
    TEST_ASSERT_EQ(128, args.buffer_size, "Configured buffer size loaded");
    TEST_ASSERT_STR_EQ("test", args.output_device_name, "Device name loaded");
    const char *bad[] = {"[audio]\nbuffer_size = abc\n", "[audio]\nsample_rate = 99999999999999999999999\n",
        "[audio]\ninput_channels = -1\n", "[audio]\noutput_channels = 65\n", "[audio]\nbuffer_size = 127\n",
        "[audio]\nsample_rate = 0\n", "[audio]\nbuffer_size = 128garbage\n", "[performance]\nrt_priority = 100\n"};
    for (const char *content : bad) {
        write(content);
        TEST_ASSERT(pw_asio_load_config(&args, path) < 0, "Malformed or unsupported config rejected without throwing");
        TEST_ASSERT_EQ(128, args.buffer_size, "Failed load preserves caller configuration");
    }
    TEST_ASSERT_EQ(42, PwHelper::env_to_uint("4294967296", 42), "Overflowing environment value rejected");
    TEST_ASSERT_EQ(42, PwHelper::env_to_uint("-1", 42), "Negative environment value rejected");
    unlink(path);
}

static void test_helper_state_and_lifetime() {
    using namespace PwHelper;
    Helper helper;
    helper.init_state.store(InitState::Running);
    helper.roundtrip_state.store(17);
    roundtrip_handler(&helper, PW_ID_CORE, 17);
    TEST_ASSERT(helper.init_state.load() == InitState::Running, "Later sync replies preserve helper running state");
    helper.init_state.store(InitState::Init);
    roundtrip_handler(&helper, PW_ID_CORE, 16);
    TEST_ASSERT(helper.init_state.load() == InitState::Init, "Unrelated sync replies do not complete initialization");
    roundtrip_handler(&helper, PW_ID_CORE, 17);
    TEST_ASSERT(helper.init_state.load() == InitState::Ready, "Matching initialization sync completes readiness");
    for (int i = 0; i < 20; ++i) {
        DefaultNodes nodes;
        nodes.vtable = &DefaultNodes::s_handler;
        nodes.default_sink = "sink";
    }
    TEST_ASSERT(true, "Default metadata destruction completes without recursion");
    Node node;
    const struct spa_dict_item items[] = {{PW_KEY_NODE_NAME, "sink"}};
    struct spa_dict props = SPA_DICT_INIT(items, 1);
    struct pw_node_info info = {};
    info.props = &props;
    node.update(&info);
    info.props = nullptr;
    node.update(&info);
    TEST_ASSERT_STR_EQ("sink", node.properties[PW_KEY_NODE_NAME].c_str(), "Partial node info updates preserve owned properties");
    TEST_ASSERT(node.info.props == nullptr, "Node cache does not retain borrowed dictionaries");
}

int main() {
    test_config_parser();
    test_helper_state_and_lifetime();
    TEST_END();
}
