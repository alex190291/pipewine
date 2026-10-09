#include "test_common.h"
#include "../gui/gui.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtCore/QTimer>

static int applied;
static void load(struct pwasio_gui_conf *conf) {
    conf->cf_buffer_size = 128;
    conf->cf_sample_rate = 48000;
    conf->cf_input_channels = conf->cf_output_channels = 4;
    conf->cf_auto_connect = false;
}
static void apply(struct pwasio_gui_conf *conf) { ++applied; }
static void close_modal(bool accept) {
    QTimer::singleShot(0, [accept]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QDialog *>(widget)) {
                if (accept) dialog->accept();
                else dialog->reject();
            }
        }
    });
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    struct pwasio_gui_conf conf = {};
    conf.load_config = load;
    conf.apply_config = apply;
    for (bool accept : {false, true, false}) {
        close_modal(accept);
        auto *gui = pwasio_init_gui(&conf);
        TEST_ASSERT_NOT_NULL(gui, "Modal dialog returns a live owned handle after closing");
        pwasio_destroy_gui(gui);
        TEST_ASSERT(QApplication::instance() == &app, "Dialog disposal preserves the caller's QApplication");
    }
    TEST_ASSERT_EQ(1, applied, "Configuration applied exactly once on acceptance");
    TEST_END();
}
