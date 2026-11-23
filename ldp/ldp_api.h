#pragma once
#if defined(_WIN32)
    #define LDP_API __declspec(dllexport)
#else
    #define LDP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

LDP_API void* ldp_create_session(const char* key);
LDP_API int ldp_send_file(void* session, const char* file_path, const char* target_ip, int port);
LDP_API int ldp_receive_file(void* session, const char* out_path, int port);
LDP_API int ldp_list_dir(void* session, const char* path, char* out_buffer, int buf_len);
LDP_API void ldp_destroy_session(void* session);
LDP_API int ldp_mkdir(void* session, const char* path);
LDP_API int ldp_rm(void* session, const char* path);
LDP_API int ldp_kill_switch(void* session);
LDP_API int ldp_audit_log(void* session, const char* user, const char* action, const char* status);
LDP_API int ldp_rename(void* session, const char* old_path, const char* new_path);
LDP_API int ldp_rmdir(void* session, const char* path);
LDP_API int ldp_chmod(void* session, const char* path, int permissions);
LDP_API int ldp_chown(void* session, const char* path, int uid, int gid);
LDP_API int ldp_stat(void* session, const char* path, char* out_buffer, int buf_len);
LDP_API int ldp_symlink(void* session, const char* target, const char* link);
LDP_API int ldp_resume_file(void* session, const char* file_path, const char* target_ip, int port, long offset);
LDP_API int ldp_atomic_send_file(void* session, const char* file_path, const char* target_ip, int port);
LDP_API int ldp_set_error_handler(void* session, void(*handler)(int code, const char* msg));
LDP_API int ldp_set_transport(void* session, int use_tcp);

#ifdef __cplusplus
}
#endif
