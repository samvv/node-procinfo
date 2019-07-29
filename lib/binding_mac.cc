
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/event.h>

#include <node_api.h>

#include <unordered_map>

#define NAPI_KQUEUE_BUFSIZE 32

static int kq = -1;

#define NAPI_TO_STRING_HELPER(x) #x
#define NAPI_TO_STRING(x) NAPI_TO_STRING_HELPER(x)

#define MAKE_CASE(enum) case enum: return #enum;

const char* strerrid(int num) {
  switch (num) {
    MAKE_CASE(EPERM)
    MAKE_CASE(ENOENT)
    MAKE_CASE(ESRCH)
    MAKE_CASE(EINTR)
    MAKE_CASE(EIO)
    MAKE_CASE(ENXIO)
    MAKE_CASE(E2BIG)
    MAKE_CASE(ENOEXEC)
    MAKE_CASE(EBADF)
    MAKE_CASE(ECHILD)
    MAKE_CASE(EDEADLK)
    MAKE_CASE(ENOMEM)
    MAKE_CASE(EACCES)
    MAKE_CASE(EFAULT)
    MAKE_CASE(ENOTBLK)
    MAKE_CASE(EBUSY)
    MAKE_CASE(EEXIST)
    MAKE_CASE(EXDEV)
    MAKE_CASE(ENODEV)
    MAKE_CASE(ENOTDIR)
    MAKE_CASE(EISDIR)
    MAKE_CASE(EINVAL)
    MAKE_CASE(ENFILE)
    MAKE_CASE(EMFILE)
    MAKE_CASE(ENOTTY)
    MAKE_CASE(ETXTBSY)
    MAKE_CASE(EFBIG)
    MAKE_CASE(ENOSPC)
    MAKE_CASE(ESPIPE)
    MAKE_CASE(EROFS)
    MAKE_CASE(EMLINK)
    MAKE_CASE(EPIPE)
    MAKE_CASE(EDOM)
    MAKE_CASE(ERANGE)
    MAKE_CASE(EAGAIN)
    MAKE_CASE(EINPROGRESS)
    MAKE_CASE(EALREADY)
    MAKE_CASE(ENOTSOCK)
    MAKE_CASE(EDESTADDRREQ)
    MAKE_CASE(EMSGSIZE)
    MAKE_CASE(EPROTOTYPE)
    MAKE_CASE(ENOPROTOOPT)
    MAKE_CASE(EPROTONOSUPPORT)
    MAKE_CASE(ESOCKTNOSUPPORT)
    MAKE_CASE(ENOTSUP)
    MAKE_CASE(EPFNOSUPPORT)
    MAKE_CASE(EAFNOSUPPORT)
    MAKE_CASE(EADDRINUSE)
    MAKE_CASE(EADDRNOTAVAIL)
    MAKE_CASE(ENETDOWN)
    MAKE_CASE(ENETUNREACH)
    MAKE_CASE(ENETRESET)
    MAKE_CASE(ECONNABORTED)
    MAKE_CASE(ECONNRESET)
    MAKE_CASE(ENOBUFS)
    MAKE_CASE(EISCONN)
    MAKE_CASE(ENOTCONN)
    MAKE_CASE(ESHUTDOWN)
    MAKE_CASE(ETIMEDOUT)
    MAKE_CASE(ECONNREFUSED)
    MAKE_CASE(ELOOP)
    MAKE_CASE(ENAMETOOLONG)
    MAKE_CASE(EHOSTDOWN)
    MAKE_CASE(EHOSTUNREACH)
    MAKE_CASE(ENOTEMPTY)
    MAKE_CASE(EPROCLIM)
    MAKE_CASE(EUSERS)
    MAKE_CASE(EDQUOT)
    MAKE_CASE(ESTALE)
    MAKE_CASE(EBADRPC)
    MAKE_CASE(ERPCMISMATCH)
    MAKE_CASE(EPROGUNAVAIL)
    MAKE_CASE(EPROGMISMATCH)
    MAKE_CASE(EPROCUNAVAIL)
    MAKE_CASE(ENOLCK)
    MAKE_CASE(ENOSYS)
    MAKE_CASE(EFTYPE)
    MAKE_CASE(EAUTH)
    MAKE_CASE(ENEEDAUTH)
    MAKE_CASE(EPWROFF)
    MAKE_CASE(EDEVERR)
    MAKE_CASE(EOVERFLOW)
    MAKE_CASE(EBADEXEC)
    MAKE_CASE(EBADARCH)
    MAKE_CASE(ESHLIBVERS)
    MAKE_CASE(EBADMACHO)
    MAKE_CASE(ECANCELED)
    MAKE_CASE(EIDRM)
    MAKE_CASE(ENOMSG)
    MAKE_CASE(EILSEQ)
    MAKE_CASE(ENOATTR)
    MAKE_CASE(EBADMSG)
    MAKE_CASE(EMULTIHOP)
    MAKE_CASE(ENODATA)
    MAKE_CASE(ENOLINK)
    MAKE_CASE(ENOSR)
    MAKE_CASE(ENOSTR)
    MAKE_CASE(EPROTO)
    MAKE_CASE(ETIME)
    MAKE_CASE(EOPNOTSUPP)
    default: return NULL;
  }
}

#define NAPI_THROW(errcode, errmsg) \
  bool is_pending;                                                   \
  napi_is_exception_pending(env, &is_pending);                       \
  if (!is_pending) {                                                 \
    napi_throw_error(env, errcode, errmsg);                          \
  }

#define NAPI_THROW_SYSCALL(errnum) \
  NAPI_THROW(strerrid(errnum), strerror(errnum))


// Macro to be called in functions that return a promise
//
// Expected declared variables:
//
//   promise           The promise that will be returned from the async
//                     function.
//   deferred          A napi_deferred that is bound to `promise`
//
// #define NAPI_CALL_ASYNC(call)                                      \
//   if ((call) != napi_ok) {                                         \
//     const napi_extended_error_info* error_info = NULL;             \
//     napi_get_last_error_info((env), &error_info);                  \
//     const char* message = (error_info->error_message == NULL)      \
//         ? "empty error message"                                    \
//         : error_info->error_message;                               \
//     napi_error_deferred(env, deferred, NULL, message);             \
//     return promise;                                                \
//   }

// Macro to be called in custom NAPI-like procedures
//
// Expected declared variables:
//
//   status          A variable to temporarily store the result to a native
//                   NAPI procedure.
//
#define NAPI_PROC_CALL(call)                                    \
  if ((status = (call)) != napi_ok) {                           \
    return status;                                              \
  }

#define NAPI_CALL_HOOK(call, exit)                              \
  if ((call) != napi_ok) {                                      \
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
    exit;                                                       \
  }

// Macro to be called in standard functions that return a napi_value
#define NAPI_CALL(call)          NAPI_CALL_HOOK(call, return NULL;)

// Macro to be called in napi_async_execute_callback and napi_async_complete_callback
#define NAPI_ASYNC_CB_CALL(call) NAPI_CALL_HOOK(call, return;)

// Macro to extract information contained within a napi_callback_info
//
// Newly created variables:
//
//    this_arg           A napi_value pointing to the context with which the
//                       function was called.
//    argv               An array of napi_values containing function arguments,
//                       in order.
//    argc               The amount of arguments that is within argv.
//
#define NAPI_EXTRACT_ARGS(min_args)                                    \
  napi_value this_arg;                                                 \
  size_t argc;                                                         \
  NAPI_CALL(napi_get_cb_info(env, cb, &argc, NULL, NULL, NULL));  \
  if (argc < min_args) {                                               \
    napi_throw_error(env, NULL, "Not enough arguments.");              \
    return NULL;                                                       \
  }                                                                    \
  napi_value argv[argc];                                               \
  void* data;                                                          \
  NAPI_CALL(napi_get_cb_info(env, cb, &argc, argv, &this_arg, &data));

#define NAPI_ARRAY_LENGTH(arr) \
  (sizeof(arr) / sizeof(*arr))

// This function does the same as napi_throw_error, but uses a napi_deferred
// object to actually propagate the error.
// napi_status napi_error_deferred(
//     napi_env env,
//     napi_deferred deferred, 
//     const char* const code, 
//     const char*const msg
// ) {
// 
//   napi_status status;
// 
//   napi_value error;
//   napi_value err_msg;
//   napi_value err_code;
// 
//   NAPI_PROC_CALL(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &err_msg));
// 
//   if (code != NULL)
//     NAPI_PROC_CALL(napi_create_string_utf8(env, msg, NAPI_AUTO_LENGTH, &err_code));
// 
//   NAPI_PROC_CALL(napi_create_error(env, code == NULL ? NULL : err_code, err_msg, &error));
//   NAPI_PROC_CALL(napi_reject_deferred(env, deferred, error));
// 
//   return napi_ok;
// }

struct JSCallback {

  napi_ref this_arg;
  napi_ref fn;

  JSCallback() {};
  JSCallback(JSCallback&& cb) = default;
  JSCallback(const JSCallback& cb) = default;

  inline JSCallback(napi_ref this_arg, napi_ref fn):
    this_arg(this_arg), fn(fn) {}

};

struct PollData {
  struct kevent kevs[NAPI_KQUEUE_BUFSIZE];
  int num_kevs;
  std::unordered_map<pid_t, JSCallback> observers;
  napi_async_work work = NULL;
  int running = false;
};

static PollData pd;

void PollExecute(napi_env env, void* data) {
  pd.num_kevs = kevent(kq, NULL, 0, pd.kevs, NAPI_KQUEUE_BUFSIZE, NULL);
}

void PollComplete(napi_env env, napi_status status, void* data) {

  if (status == napi_cancelled) {
    pd.running = false;
    return;
  }

  if (pd.num_kevs == -1) {

    pd.running = false;
    NAPI_THROW_SYSCALL(pd.num_kevs);
    return;

  } else {

    for (int i = 0; i < pd.num_kevs; i++) {

      switch (pd.kevs[i].filter) {

        case EVFILT_USER:
          if (pd.kevs[i].fflags & 0x1) {
            pid_t pid = pd.kevs[i].ident;
            pd.observers.erase(pid);
          }
          break;

        case EVFILT_PROC:

          if (pd.kevs[i].fflags & NOTE_EXIT) {
            napi_value exit_str;
            napi_value exit_code;
            pid_t pid = pd.kevs[i].ident;
            NAPI_ASYNC_CB_CALL(napi_create_int32(env, pd.kevs[i].data, &exit_code));
            NAPI_ASYNC_CB_CALL(napi_create_string_utf8(env, "exit", NAPI_AUTO_LENGTH, &exit_str));
            napi_value emit_args[] = { exit_str, exit_code };
            JSCallback cb = pd.observers[pid];
            napi_value this_arg;
            napi_value emit;
            NAPI_ASYNC_CB_CALL(napi_get_reference_value(env, cb.this_arg, &this_arg));
            NAPI_ASYNC_CB_CALL(napi_get_reference_value(env, cb.fn, &emit));
            NAPI_ASYNC_CB_CALL(napi_call_function(env, this_arg, emit, NAPI_ARRAY_LENGTH(emit_args), emit_args, NULL));
            pd.observers.erase(pid);
            NAPI_ASYNC_CB_CALL(napi_delete_reference(env, cb.this_arg));
            NAPI_ASYNC_CB_CALL(napi_delete_reference(env, cb.fn));
          }
          break;

      }
    }

  }

  if (pd.observers.empty()) {
    napi_async_work work = pd.work;
    pd.work = NULL;
    pd.running = false;
    NAPI_ASYNC_CB_CALL(napi_delete_async_work(env, work));
  } else {
    NAPI_ASYNC_CB_CALL(napi_queue_async_work(env, pd.work));
  }

}

napi_status napi_inherits(napi_env env, napi_value base, napi_value super) {

  napi_status status;
  napi_value object;
  napi_value object_set_proto;
  napi_value super_proto;
  napi_value base_proto;

  NAPI_PROC_CALL(napi_get_named_property(env, super, "prototype", &super_proto));
  NAPI_PROC_CALL(napi_get_named_property(env, base, "prototype", &base_proto));

  NAPI_PROC_CALL(napi_get_global(env, &object));
  NAPI_PROC_CALL(napi_get_named_property(env, object, "Object", &object));
  NAPI_PROC_CALL(napi_get_named_property(env, object, "setPrototypeOf", &object_set_proto))

  napi_value set_args[] = { base_proto, super_proto };
  NAPI_PROC_CALL(napi_call_function(env, object, object_set_proto, NAPI_ARRAY_LENGTH(set_args), set_args, NULL));

  return napi_ok;

}

#define NAPI_ASSERT_NUMBER(value, errmsg)                  \
  { napi_valuetype napi_valuetype__;                       \
    NAPI_CALL(napi_typeof(env, value, &napi_valuetype__)); \
    if (napi_valuetype__ != napi_number) {                 \
      napi_throw_error(env, NULL, errmsg);                 \
      return NULL;                                         \
    } }

int IsProcessRunning(pid_t pid, bool* out) {

  // This MIB array will get passed to sysctl()
  // See man 3 systcl for details
  int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };

  struct kinfo_proc result;
  size_t oldp_len = sizeof(result);

  // sysctl() refuses to fill the buffer if the PID does not exist,
  // so the only way to detect failure is to set all fields to 0
  memset(&result, 0, sizeof(struct kinfo_proc));

  int status = sysctl(name, NAPI_ARRAY_LENGTH(name), &result, &oldp_len, NULL, 0);

  if (status < 0) { 
    return status;
  }

  // Possible values for kp_proc.p_stat are:
  //
  //   SIDL      Process being created by fork.
  //   SRUN      Currently runnable.
  //   SSLEEP    Sleeping on an address.
  //   SSTOP     Process debugging or suspension.
  //   SZOMB     Awaiting collection by parent.
  //
  // All values are accepted, except for zombie processes, which
  // are effectively 'dead'.
  //
  *out = result.kp_proc.p_pid > 0 && result.kp_proc.p_stat != SZOMB;

  return 0;

}

napi_value ProcessNew(napi_env env, napi_callback_info cb) {

  pid_t pid;
  napi_value emit;
  napi_value super;
  struct kevent kev;
  bool is_running;

  // Initialize this_arg, argv, argc, pid
  NAPI_EXTRACT_ARGS(1)
  NAPI_ASSERT_NUMBER(argv[0], "First argument must be a number.");
  NAPI_CALL(napi_get_value_int32(env, argv[0], &pid));

  // Check that the process we're trying to connect to actually exists

  if (IsProcessRunning(pid, &is_running) < 0) {
    NAPI_THROW_SYSCALL(errno);
    return NULL;
  }

  if (!is_running) {
    NAPI_THROW(NULL, "Could not connect to process: process is not running");
    return NULL;
  }

  NAPI_CALL(napi_get_reference_value(env, (napi_ref)data, &super));

  // NAPI_CALL(napi_get_named_property(env, (napi_value)data, "constructor", &super_cons));
  NAPI_CALL(napi_call_function(env, this_arg, super, 0, NULL, NULL));

  NAPI_CALL(napi_set_named_property(env, this_arg, "pid", argv[0]));

  NAPI_CALL(napi_get_named_property(env, this_arg, "emit", &emit));

  EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_EXIT, 0, NULL);

  if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1) {

    NAPI_THROW_SYSCALL(errno);

  } else {

    napi_ref emit_ref;
    napi_ref this_arg_ref;

    NAPI_CALL(napi_create_reference(env, emit, 1, &emit_ref));
    NAPI_CALL(napi_create_reference(env, this_arg, 1, &this_arg_ref));

    pd.observers.emplace(pid, JSCallback(this_arg_ref, emit_ref));

    if (!pd.running) {
      if (pd.work == NULL) {
        napi_value work_name;
        NAPI_CALL(napi_create_string_utf8(env, "PROCPOLL", NAPI_AUTO_LENGTH, &work_name));
        NAPI_CALL(napi_create_async_work(env, NULL, work_name, PollExecute, PollComplete, NULL, &pd.work));
      }
      pd.running = true;
      NAPI_CALL(napi_queue_async_work(env, pd.work));
    }

  }

  return NULL;
}

napi_value ProcessClose(napi_env env, napi_callback_info cb) {

  pid_t pid;
  napi_value pid_value;

  // Initialize argv, argc, this_arg, pid
  NAPI_EXTRACT_ARGS(0);
  NAPI_CALL(napi_get_named_property(env, this_arg, "pid", &pid_value));
  NAPI_ASSERT_NUMBER(pid_value, "Process.pid is not a number.");
  NAPI_CALL(napi_get_value_int32(env, pid_value, &pid));

  struct kevent kev;

  EV_SET(&kev, pid, EVFILT_USER, EV_ADD | EV_ONESHOT, NOTE_TRIGGER | 0x1, 0, NULL);

  if (kevent(kq, &kev, 1, NULL, 0, NULL) < 0) {
    NAPI_THROW_SYSCALL(errno);
  }

  return NULL;
}

napi_value ProcessGetRunning(napi_env env, napi_callback_info cb) {

  pid_t pid;
  napi_value pid_value;
  bool is_running;
  napi_value out;

  // Initialize argv, argc, this_arg, pid
  NAPI_EXTRACT_ARGS(0);
  NAPI_CALL(napi_get_named_property(env, this_arg, "pid", &pid_value));
  NAPI_ASSERT_NUMBER(pid_value, "Process.pid is not a number.");
  NAPI_CALL(napi_get_value_int32(env, pid_value, &pid));

  if (IsProcessRunning(pid, &is_running) < 0) {
    NAPI_THROW_SYSCALL(errno);
    return NULL;
  }

  NAPI_CALL(napi_get_boolean(env, is_running, &out));

  return out;

}

napi_value Init(napi_env env, napi_callback_info cb) {

  napi_value exports;
  napi_value process_cons;
  napi_ref super_ref;

  NAPI_EXTRACT_ARGS(1);

  NAPI_CALL(napi_create_object(env, &exports));

  napi_property_descriptor process_props[] = {
    { "running", NULL, NULL, ProcessGetRunning, NULL, NULL, napi_default, NULL },
    { "close", NULL, ProcessClose, NULL, NULL, NULL, napi_default, NULL }
  };

  NAPI_CALL(napi_create_reference(env, argv[0], 1, &super_ref));
  NAPI_CALL(napi_define_class(env, "Process", NAPI_AUTO_LENGTH, ProcessNew, super_ref, NAPI_ARRAY_LENGTH(process_props), process_props, &process_cons));
  NAPI_CALL(napi_inherits(env, process_cons, argv[0]));

  napi_property_descriptor props[] = {
    { "Process", NULL, NULL, NULL, NULL, process_cons, napi_default, NULL },
    // { "processIsRunning", NULL, process_is_running, NULL, NULL, NULL, napi_default, NULL },
    // { "waitProcessComplete", NULL, processes_wait_complete, NULL, NULL, NULL, napi_default, NULL }
  };
  NAPI_CALL(napi_define_properties(env, exports, NAPI_ARRAY_LENGTH(props), props));

  return exports;
}

napi_value Bind(napi_env env, napi_value exports) {

  if (kq == -1) {
    kq = kqueue();
    if (kq == -1) {
      NAPI_THROW_SYSCALL(errno);
      return exports;
    }
  }

  NAPI_CALL(napi_create_function(env, "bind", NAPI_AUTO_LENGTH, Init, NULL, &exports));

  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Bind)


