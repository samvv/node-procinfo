
#include <string.h>

#include <sys/event.h>
#include <unistd.h>

#include <node_api.h>

#include <CoreFoundation/CoreFoundation.h>

#define ADDON_KQ_BUFSIZE 32

/* static void noteProcDeath(CFFileDescriptorRef fdref, CFOptionFlags callBackTypes, void *info) { */
/*     struct kevent kev; */
/*     int fd = CFFileDescriptorGetNativeDescriptor(fdref); */
/*     kevent(fd, NULL, 0, &kev, 1, NULL); */
/*     // take action on death of process here */
/*     printf("process with pid '%u' died\n", (unsigned int)kev.ident); */
/*     CFFileDescriptorInvalidate(fdref); */
/*     CFRelease(fdref); // the CFFileDescriptorRef is no longer of any use in this example */
/* } */
/*  */
/* int main(int argc, char *argv[]) { */
/*  */
/*     if (argc < 2) exit(1); */
/*  */
/*     int fd = kqueue(); */
/*  */
/*     struct kevent kev; */
/*     EV_SET(&kev, atoi(argv[1]), EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0, NULL); */
/*     kevent(fd, &kev, 1, NULL, 0, NULL); */
/*     CFFileDescriptorRef fdref = CFFileDescriptorCreate(kCFAllocatorDefault, fd, true, noteProcDeath, NULL); */
/*     CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack); */
/*     CFRunLoopSourceRef source = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0); */
/*     CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode); */
/*     CFRelease(source); */
/*  */
/*     // run the run loop for 20 seconds */
/*     CFRunLoopRunInMode(kCFRunLoopDefaultMode, 20.0, false); */
/*  */
/*     return 0; */
/* } */

static int kq = -1;

#define ADDON_TO_STRING(x) ADDON_TO_STRING_HELPER(x)
#define ADDON_TO_STRING_HELPER(x) #x

#define NAPI_CALL(call)                                           \
  do {                                                            \
    napi_status status = (call);                                  \
    if (status != napi_ok) {                                      \
      const napi_extended_error_info* error_info = NULL;          \
      napi_get_last_error_info((env), &error_info);               \
      bool is_pending;                                            \
      napi_is_exception_pending((env), &is_pending);              \
      if (!is_pending) {                                          \
        const char* message = (error_info->error_message == NULL) \
            ? "empty error message"                               \
            : error_info->error_message;                          \
        napi_throw_error((env), NULL, message);                   \
        return NULL;                                              \
      }                                                           \
    }                                                             \
  } while(0)


#define NAPI_VOID_CALL(call)                                      \
  do {                                                            \
    napi_status status = (call);                                  \
    if (status != napi_ok) {                                      \
      const napi_extended_error_info* error_info = NULL;          \
      napi_get_last_error_info((env), &error_info);               \
      bool is_pending;                                            \
      napi_is_exception_pending((env), &is_pending);              \
      if (!is_pending) {                                          \
        const char* message = (error_info->error_message == NULL) \
            ? "empty error message"                               \
            : error_info->error_message;                          \
        napi_throw_error((env), NULL, message);                   \
      }                                                           \
    }                                                             \
  } while(0)

#define ADDON_VOID_SYSCALL(stmt) \
  if ((stmt) == -1) { \
    napi_throw_error(env, NULL, __FILE__ ":" ADDON_TO_STRING(__LINE__) ": system call " #stmt " failed."); \
    return; \
  }

#define ADDON_SYSCALL(stmt) \
  if ((stmt) == -1) { \
    perror(#stmt); \
    napi_throw_error(env, NULL, __FILE__ ":" ADDON_TO_STRING(__LINE__) ": system call " #stmt " failed."); \
    return NULL; \
  }

#define ADDON_PROPS_LENGTH(props) \
  (sizeof(props) / sizeof(napi_property_descriptor))

napi_value procinfo_create_process(napi_env env, napi_callback_info cb) {
  size_t argc;
  NAPI_CALL(napi_get_cb_info(env, cb, &argc, NULL, NULL, NULL));
  if (argc < 1) {
    napi_throw_error(env, NULL, "Not enough arguments.");
    return NULL;
  }
  napi_value argv[argc];
  napi_value this_arg;
  NAPI_CALL(napi_get_cb_info(env, cb, NULL, argv, &this_arg, NULL));
  NAPI_CALL(napi_set_named_property(env, this_arg, "pid", argv[0]));
  return NULL;
}

struct poll_data {
  napi_async_work work;
  napi_deferred deferred;
};

void poll(napi_env env, void* data) {
  struct poll_data* pd = data;
  struct kevent kevs[ADDON_KQ_BUFSIZE];
  int nkevs;
  ADDON_VOID_SYSCALL(nkevs = kevent(kq, NULL, 0, kevs, ADDON_KQ_BUFSIZE, NULL));
  printf("Got %i events.\n", nkevs);
  for (int i = 0; i < nkevs; i++) {
    fprintf(stderr, "flags = %i, fflags = %i\n", kevs[i].flags, kevs[i].fflags);
    if (kevs[i].fflags & NOTE_EXIT) {
      napi_value exit_code;
      NAPI_VOID_CALL(napi_create_int64(env, kevs[i].data, &exit_code));
      NAPI_VOID_CALL(napi_resolve_deferred(env, pd->deferred, exit_code));
      return;
    }
  }
  NAPI_VOID_CALL(napi_queue_async_work(env, pd->work));
}

napi_value processes_wait_complete(napi_env env, napi_callback_info cb) {
  napi_value this_arg;
  size_t argc;
  NAPI_CALL(napi_get_cb_info(env, cb, &argc, NULL, &this_arg, NULL));
  if (argc < 1) {
    napi_throw_error(env, NULL, "Not enough arguments.");
    return NULL;
  }
  napi_value argv[argc];
  NAPI_CALL(napi_get_cb_info(env, cb, &argc, argv, &this_arg, NULL));
  napi_valuetype arg0_type;
  NAPI_CALL(napi_typeof(env, argv[0], &arg0_type));
  if (arg0_type != napi_number) {
    napi_throw_error(env, NULL, "First argument must be a number.");
    return NULL;
  }
  int64_t pid;
  NAPI_CALL(napi_get_value_int64(env, argv[0], &pid));
  napi_value promise;
  napi_deferred deferred;
  NAPI_CALL(napi_create_promise(env, &deferred, &promise));
  struct poll_data* data = malloc(sizeof(struct poll_data));
  data->deferred = deferred;
  napi_value work_name;
  struct kevent kev;
  EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0, NULL);
  ADDON_SYSCALL(kevent(kq, &kev, 1, NULL, 0, NULL));
  NAPI_CALL(napi_create_string_utf8(env, "PROCPOLL", NAPI_AUTO_LENGTH, &work_name));
  NAPI_CALL(napi_create_async_work(env, NULL, work_name, poll, NULL, data, &data->work));
  NAPI_CALL(napi_queue_async_work(env, data->work));
  return promise;
}

napi_value procinfo_bind(napi_env env, napi_callback_info cb) {
  napi_value exports;
  NAPI_CALL(napi_create_object(env, &exports));
  napi_property_descriptor proc_props[] = { };
  napi_value proc_constr;
  NAPI_CALL(napi_define_class(env, "Process", NAPI_AUTO_LENGTH, procinfo_create_process, NULL, ADDON_PROPS_LENGTH(proc_props), proc_props, &proc_constr));
  napi_property_descriptor props[] = {
    { "Process", NULL, NULL, NULL, NULL, proc_constr, napi_default, NULL },
    { "waitProcessComplete", NULL, processes_wait_complete, NULL, NULL, NULL, napi_default, NULL }
  };
  NAPI_CALL(napi_define_properties(env, exports, ADDON_PROPS_LENGTH(props), props));
  return exports;
}

napi_value procinfo_init(napi_env env, napi_value exports) {
  napi_value bind_fn;
  if (kq == -1) {
    ADDON_SYSCALL(kq = kqueue());
  }
  NAPI_CALL(napi_create_function(env, "bind", NAPI_AUTO_LENGTH, procinfo_bind, NULL, &bind_fn));
  return bind_fn;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, procinfo_init)

