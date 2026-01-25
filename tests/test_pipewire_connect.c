#include <stdio.h>
#include <stdlib.h>
#include "../pw_helper_c.h"
#include "../pw_helper_common.h"

int main(void) {
    printf("PipeWire Connection Test\n");
    printf("=========================\n\n");

    // Initialize default config
    struct pw_helper_init_args config;
    pw_asio_init_default_config(&config);

    printf("1. Initializing PipeWire helper...\n");
    printf("   - app_name: %s\n", config.app_name ? config.app_name : "(default)");
    printf("   - buffer_size: %u\n", config.buffer_size);
    printf("   - sample_rate: %u\n", config.sample_rate);
    printf("   - input_channels: %u\n", config.num_input_channels);
    printf("   - output_channels: %u\n", config.num_output_channels);

    struct user_pw_helper *helper = user_pw_create_helper(0, NULL, &config);

    if (!helper) {
        printf("\n❌ FAILED: Could not create PipeWire helper!\n");
        printf("\nPossible causes:\n");
        printf("  - PipeWire service not running\n");
        printf("  - Permission issues\n");
        printf("  - PipeWire socket not accessible\n");
        printf("\nCheck: systemctl --user status pipewire\n");
        printf("Check: ls -la $XDG_RUNTIME_DIR/pipewire-0\n");
        return 1;
    }

    printf("✅ SUCCESS: PipeWire helper created!\n\n");

    printf("2. Checking default nodes...\n");
    struct pw_node *input_node = user_pw_get_default_node(helper, 0);  // SPA_DIRECTION_INPUT
    struct pw_node *output_node = user_pw_get_default_node(helper, 1); // SPA_DIRECTION_OUTPUT

    if (input_node) {
        printf("   ✅ Default input node found\n");
    } else {
        printf("   ⚠️  No default input node\n");
    }

    if (output_node) {
        printf("   ✅ Default output node found\n");
    } else {
        printf("   ⚠️  No default output node\n");
    }

    printf("\n3. Cleaning up...\n");
    user_pw_destroy_helper(helper);
    printf("   ✅ Helper destroyed\n");

    printf("\n========================================\n");
    printf("PipeWire connectivity test PASSED!\n");
    printf("The driver should work in Ableton.\n");
    printf("========================================\n");

    return 0;
}
