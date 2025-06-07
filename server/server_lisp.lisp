(ffi:clines "#include \"../include/server_capi.h\"")
(defun shutdown ()
  (ffi:c-inline () () :void "do_shutdown();"))
