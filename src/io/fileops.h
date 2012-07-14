
#define MVM_encoding_type_utf8 1
#define MVM_encoding_type_ascii 2
#define ENCODING_VALID(enc) (((enc) >= MVM_encoding_type_utf8 && (enc) <= MVM_encoding_type_ascii) \
                            || MVM_exception_throw_adhoc(tc, "%d is not a valid encoding type flag"))

MVMObject * MVM_file_get_anon_oshandle_type(MVMThreadContext *tc);
char * MVM_file_get_full_path(MVMThreadContext *tc, apr_pool_t *tmp_pool, char *path);
void MVM_file_copy(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_append(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_rename(MVMThreadContext *tc, MVMString *src, MVMString *dest);
void MVM_file_delete(MVMThreadContext *tc, MVMString *f);
void MVM_file_chmod(MVMThreadContext *tc, MVMString *f, MVMint64 flag);
MVMint64 MVM_file_exists(MVMThreadContext *tc, MVMString *f);
MVMObject * MVM_file_open_fh(MVMThreadContext *tc, MVMObject *type_object, MVMString *filename, MVMint64 flag, MVMint64 encoding_flag);
void MVM_file_close_fh(MVMThreadContext *tc, MVMObject *oshandle);
MVMString * MVM_file_read_fhs(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 length);
MVMString * MVM_file_slurp(MVMThreadContext *tc, MVMString *filename, MVMint64 encoding_flag);
void MVM_file_spew(MVMThreadContext *tc, MVMString *output, MVMString *filename, MVMint64 encoding_flag);
void MVM_file_seek(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset, MVMint64 flag);
MVMint64 MVM_file_lock(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 flag);
void MVM_file_unlock(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_file_flush(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_file_sync(MVMThreadContext *tc, MVMObject *oshandle);
void MVM_file_pipe(MVMThreadContext *tc, MVMObject *oshandle1, MVMObject *oshandle2);
void MVM_file_truncate(MVMThreadContext *tc, MVMObject *oshandle, MVMint64 offset);
MVMint64 MVM_file_eof(MVMThreadContext *tc, MVMObject *oshandle);
MVMObject * MVM_file_get_stdin(MVMThreadContext *tc, MVMObject *type_object, MVMint64 encoding_flag);
MVMObject * MVM_file_get_stdout(MVMThreadContext *tc, MVMObject *type_object, MVMint64 encoding_flag);
MVMObject * MVM_file_get_stderr(MVMThreadContext *tc, MVMObject *type_object, MVMint64 encoding_flag);
