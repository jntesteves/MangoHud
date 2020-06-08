#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include "config.h"
#include "notify.h"

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

static void fileChanged(void *params_void) {
    notify_thread *nt = reinterpret_cast<notify_thread *>(params_void);
    int length, i = 0;
    char buffer[EVENT_BUF_LEN];
    overlay_params local_params = *nt->params;

    while (!nt->quit) {
        length = read( nt->fd, buffer, EVENT_BUF_LEN );
        while (i < length) {
            struct inotify_event *event =
                (struct inotify_event *) &buffer[i];
            i += EVENT_SIZE + event->len;
            if (event->mask & IN_MODIFY /*|| event->mask & IN_IGNORED*/) {
                parse_overlay_config(&local_params, getenv("MANGOHUD_CONFIG"));
                std::lock_guard<std::mutex> lk(nt->mutex);
                /*if (nt->params->config_file_path != local_params.config_file_path) {
                    fprintf(stderr, "MANGOHUD: watching config file: %s\n", local_params.config_file_path.c_str());
                    inotify_rm_watch(nt->fd, nt->wd);
                    nt->wd = inotify_add_watch(nt->fd, local_params.config_file_path.c_str(), IN_MODIFY | IN_DELETE | IN_DELETE_SELF);
                }*/
                *nt->params = local_params;
            }
        }
        i = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool start_notifier(notify_thread& nt)
{
    nt.fd = inotify_init();
    nt.wd = inotify_add_watch( nt.fd, nt.params->config_file_path.c_str(), IN_MODIFY);

    int flags = fcntl(nt.fd, F_GETFL, 0);
    if (fcntl(nt.fd, F_SETFL, flags | O_NONBLOCK))
        perror("Set non-blocking failed");

    if (nt.wd < 0) {
        close(nt.fd);
        nt.fd = -1;
        return false;
    }

    if (nt.thread.joinable())
        nt.thread.join();
    nt.thread = std::thread(fileChanged, &nt);
    return true;
}

void stop_notifier(notify_thread& nt)
{
    if (nt.fd < 0)
        return;

    nt.quit = true;
    if (nt.thread.joinable())
        nt.thread.join();
    inotify_rm_watch(nt.fd, nt.wd);
    close(nt.fd);
    nt.fd = -1;
}
