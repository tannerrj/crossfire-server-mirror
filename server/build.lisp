(compile-file "server_lisp.lisp" :system-p t)
(c:build-static-library "server_lisp"
                        :lisp-files '("server_lisp.o")
                        :init-name "init_lisp")
