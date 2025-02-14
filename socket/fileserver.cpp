/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 2025 the Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

 /**
  * @file
  * File server functionality.
  * 
  * This provides a file serving thread for the client to download larger files from.
  * It uses a simple protocol that only sends one file per connection. At the moment
  * it only serves files from "<data dir>/sounds"
  * (e.g., /usr/games/crossfire/share/crossfire/sounds)
  * 
  * The client can request which port the file server is using by sending a
  * "requestinfo fileport" command to the server. If the server responds with
  * an empty replyinfo, then the server doesn't support it, otherwise the client
  * will receive a replyinfo with the port number as a 32-bit unsigned integer.
  * 
  * The protocol is as follows:
  * - The client connects to the server via TCP using either the Crossfire Server's
  *   port + 1 or a specified file_port value in settings.
  * - The client sends a plain text-based request for a file with an
  *   optional checksum.
  *   - ex. "REQ filename"
  *   - ex. "REQ filename checksum"
  * - If the file exists and either no checksum is sent or the checksum does not
  *   match, the server sends 4-bytes in network byte order containing the length
  *   of the data followed by the actual data.
  * - If the checksum matches, the server sends 4 bytes equal to 0, signifying
  *   that the client already has the correct file.
  * - If the file does not exist, the server simply closes the connection.
  * 
  * Examples:
  * 
  * no checksum:
  * C->S: REQ sounds/music/navar
  * S->C: [4 bytes of length][data...] (close)
  * 
  * correct checksum:
  * C->S: REQ sounds/music/navar 123456789
  * S->C: [4 bytes containing 0] (close)
  * 
  * incorrect checksum:
  * C->S: REQ sounds/music/navar 000000000
  * S->C: [4 bytes of length][data...] (close)
  * 
  * missing file:
  * C->S: REQ sounds/music/doesnotexist
  * S->C: (close)
  */

#ifdef WIN32 /* ---win32 exclude/include headers */
#include "process.h"
#else
#ifndef CF_MXE_CROSS_COMPILE
#include <sys/stat.h>
#endif
#endif

#include "global.h"

#include <unordered_map>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <system_error>
#include <arpa/inet.h>
#include <fcntl.h>

/** Fileserver thread */
static std::thread fs_thread;

int pipe_sds[2] = {0, 0};

struct checksum {
    std::string filename;
    std::string fullpath; // Store and use for later.
    uint32_t checksum;
};

std::unordered_map<std::string, checksum> checksums;

int fileserver_build_checksums(const std::string root, const std::string path) {
    LOG(llevDebug, "fileserver_build_checksums: building checksums for %s %s\n", root.c_str(), path.c_str());

    struct dirent *entry;
    struct stat statbuf;

    DIR *dir = opendir(path.c_str());
    if (dir == nullptr) {
        LOG(llevError, "fileserver_build_checksums: could not open %s\n", path.c_str());
        return -1;
    }

    for (entry = readdir(dir); entry; entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string rootpath = root + "/" + entry->d_name;
        std::string fullpath = path + "/" + entry->d_name;

        if (stat(fullpath.c_str(), &statbuf) == -1) {
            LOG(llevError, "fileserver_build_checksums: could not stat %s\n", fullpath.c_str());
            continue;
        }
        if (S_ISDIR(statbuf.st_mode)) {
            fileserver_build_checksums(rootpath, fullpath);
        } else {
            // Ignore .LICENSE
            if (strstr(entry->d_name, ".LICENSE")) {
                continue;
            }
            // Trim extension from rootpath.
            size_t pos = rootpath.find_last_of('.');
            if (pos != std::string::npos) {
                rootpath = rootpath.substr(0, pos);
            }

            FILE *fp = fopen(fullpath.c_str(), "rb");
            if (fp == nullptr) {
                LOG(llevError, "fileserver_build_checksums: could not open %s\n", fullpath.c_str());
                continue;
            }

            uint32_t checksum = 0;
            fseek(fp, 0, SEEK_END);
            size_t len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            for (size_t i = 0; i < len; i++) {
                ROTATE_RIGHT(checksum);
                checksum += fgetc(fp);
                checksum &= 0xffffffff;
            }
            fclose(fp);
            checksums[rootpath] = {rootpath, fullpath, checksum};
        }
    }

    closedir(dir);
    return 0;
}

int fileserver_sendfile(int sd, std::string &path) {
    LOG(llevDebug, "fileserver_sendfile: sending file %s\n", path.c_str());
    FILE *fp = fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        LOG(llevError, "fileserver_sendfile: could not open %s\n", path.c_str());
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint32_t length = htonl(len);

    // Send 4-bytes containing the length.
    if (send(sd, &length, sizeof(length), 0) == -1) {
        LOG(llevError, "fileserver_sendfile: send failed, code %d, what %s\n", errno, strerror(errno));
        fclose(fp);
        return -1;
    }

    // Send the actual payload in 1024 byte chunks.
    char buf[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (send(sd, buf, bytes_read, 0) == -1) {
            LOG(llevError, "fileserver_sendfile: send failed, code %d, what %s\n", errno, strerror(errno));
            fclose(fp);
            return -1;
        }
    }
    return 0;
}

void fileserver_thread(int sd) {
    fd_set main;
    fd_set read_fds;
    int fdmax;

    int newfd; // accept() fd
    struct sockaddr_storage raddr; // client address
    socklen_t raddrlen;

    char req_buf[MAX_BUF]; // Buffer for receive.
    int rbytes; // bytes read from ^

    int yes = 1;

    FD_ZERO(&main);
    FD_ZERO(&read_fds);

    if (pipe(pipe_sds) == -1) {
        LOG(llevError, "fileserver_thread: pipe failed, code %d, what %s\n", errno, strerror(errno));
        return;
    }

    listen(sd, SOMAXCONN); // Not sure if we should just use 5 here.

    FD_SET(pipe_sds[0], &main);
    fdmax = std::max(fdmax, pipe_sds[0]+1);

    int flags = fcntl(pipe_sds[0], F_GETFL);
    if (flags == -1) {
        LOG(llevError, "fileserver_thread: fcntl failed, code %d, what %s\n", errno, strerror(errno));
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(pipe_sds[0], F_SETFL, flags) == -1) {
        LOG(llevError, "fileserver_thread: fcntl failed, code %d, what %s\n", errno, strerror(errno));
        return;
    }

    flags = fcntl(pipe_sds[1], F_GETFL);
    if (flags == -1) {
        LOG(llevError, "fileserver_thread: fcntl failed, code %d, what %s\n", errno, strerror(errno));
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(pipe_sds[1], F_SETFL, flags) == -1) {
        LOG(llevError, "fileserver_thread: fcntl failed, code %d, what %s\n", errno, strerror(errno));
        return;
    }

    FD_SET(sd, &main);
    fdmax = std::max(fdmax, sd+1);

    for(;;) {
        read_fds = main;
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            LOG(llevError, "fileserver_thread: select failed, code %d, what %s\n", errno, strerror(errno));
            break;
        }

        if (FD_ISSET(pipe_sds[0], &read_fds)) {
            LOG(llevDebug, "fileserver_thread: control socket closed\n");
            for (int j = 0; j <= fdmax; j++) {
                if (FD_ISSET(j, &main)) {
                    close(j);
                }
            }
            break;
        }

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == sd) {
                    // New connection.
                    raddrlen = sizeof(raddr);
                    newfd = accept(sd, (struct sockaddr *)&raddr, &raddrlen);
                    if (newfd == -1) {
                        LOG(llevError, "fileserver_thread: accept failed, code %d, what %s\n", errno, strerror(errno));
                    } else {
                        FD_SET(newfd, &main);
                        if (newfd > fdmax) {
                            fdmax = newfd;
                        }
                        LOG(llevDebug, "fileserver_thread: new connection on socket %d\n", newfd);
                    }
                } else {
                    // Client data.
                    if ((rbytes = recv(i, req_buf, sizeof(req_buf), 0)) <= 0) {
                        // Connection closed or error.
                        if (rbytes == 0) {
                            LOG(llevDebug, "fileserver_thread: socket %d closed\n", i);
                        } else {
                            LOG(llevError, "fileserver_thread: recv failed, code %d, what %s\n", errno, strerror(errno));
                        }
                        close(i);
                        FD_CLR(i, &main);
                    } else {
                        // Process client data.
                        if (rbytes < 5) { // REQ<space><char> minimum
                            LOG(llevDebug, "fileserver_thread: too few bytes from %d, closing\n", i);
                            close(i);
                            FD_CLR(i, &main);
                            continue;
                        }

                        // Check for REQ<space>
                        if (strncmp(req_buf, "REQ ", 4) != 0) {
                            LOG(llevDebug, "fileserver_thread: invalid request from %d, closing\n", i);
                            close(i);
                            FD_CLR(i, &main);
                            continue;
                        }
                        std::string filename;
                        std::string checksum_str;

                        bool got_filename = false;
                        for (int j = 4; j < rbytes; j++) {
                            if (!got_filename) {
                                if (req_buf[j] == ' ' || req_buf[j] == '\0' || req_buf[j] == '\n' || req_buf[j] == '\r') {
                                    got_filename = true;
                                } else {
                                    filename += req_buf[j];
                                }
                            } else {
                                if (req_buf[j] == '\0' || req_buf[j] == '\n' || req_buf[j] == '\r') {
                                    break;
                                } else {
                                    checksum_str += req_buf[j];
                                }
                            }
                        }

                        uint32_t checksum = 0;
                        if (!checksum_str.empty()) {
                            checksum = strtoul(checksum_str.c_str(), NULL, 10);
                            if (errno == ERANGE) {
                                LOG(llevDebug, "fileserver_thread: invalid checksum from %d, closing\n", i);
                                close(i);
                                FD_CLR(i, &main);
                                continue;
                            }
                        }


                        auto it = checksums.find(filename);
                        if (it == checksums.end()) {
                            LOG(llevDebug, "fileserver_thread: file %s not found, closing connection\n", filename.c_str());
                            close(i);
                            FD_CLR(i, &main);
                            continue;
                        }

                        // Check if the given file matches our stored checksum.
                        if (it->second.checksum == checksum) {
                            // Same, just close the connection.
                            LOG(llevDebug, "fileserver_thread: file %s matches checksum, closing connection\n", filename.c_str());
                            uint32_t length = 0;
                            if (send(i, &length, sizeof(length), 0) == -1) {
                                LOG(llevError, "fileserver_thread: send failed, code %d, what %s\n", errno, strerror(errno));
                            }
                            close(i);
                            FD_CLR(i, &main);
                            continue;
                        } else {
                            // Different, time to send the file.
                            LOG(llevDebug, "fileserver_thread: file %s does not match checksum, sending file\n", filename.c_str());
                            if (fileserver_sendfile(i, it->second.fullpath) == -1) {
                                LOG(llevError, "fileserver_thread: failed to send file %s, closing connection\n", filename.c_str());
                                close(i);
                                FD_CLR(i, &main);
                                continue;
                            }
                            // Close connection after sending file.
                            close(i);
                            FD_CLR(i, &main);
                            LOG(llevDebug, "fileserver_thread: sent file %s\n", filename.c_str());
                        }
                    }
                }
            }
        }
    }
    close(sd);
    LOG(llevInfo, "fileserver_thread: exiting\n");
}

/**
 * Fileserver initialization logic. This sets up a port either using file_port if non-zero or on the CF port + 1 if zero.
 */
int fileserver_init(void) {
    // Load checksums for share/sounds
    std::string sounddir = std::string(settings.datadir) + "/sounds";
    fileserver_build_checksums("sounds", sounddir);

    for (auto &kv : checksums) {
        LOG(llevDebug, "fileserver_init: %s checksum %u\n", kv.second.filename.c_str(), kv.second.checksum);
    }
    LOG(llevDebug, "fileserver_init: built checksums\n");

    if (checksums.size() == 0) {
        LOG(llevError, "fileserver_init: no checksums found, not starting\n");
        return 0;
    }

    try {
        sockaddr_in file_sockaddr;

        /* Spin up the TCP server. */
        memset(&file_sockaddr, 0, sizeof(file_sockaddr));
        file_sockaddr.sin_family = AF_INET;
        file_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (settings.file_port == 0) {
            file_sockaddr.sin_port = htons(settings.csport+1);
        } else {
            file_sockaddr.sin_port = htons(settings.file_port);
        }
        LOG(llevDebug, "listening on %d\n", file_sockaddr.sin_port);

        int file_sd = socket(AF_INET, SOCK_STREAM, 0);
        if (file_sd < 0) {
            LOG(llevError, "fileserver_init: failed to create socket, code %d, what %s\n", errno, strerror(errno));
            return 0;
        }

        int yes = 1;
        setsockopt(file_sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(file_sd, (struct sockaddr *)&file_sockaddr, sizeof(file_sockaddr)) < 0) {
            LOG(llevError, "fileserver_init: failed to bind socket, code %d, what %s\n", errno, strerror(errno));
            return 0;
        }

        // Okay, we seem good, let's start our thread!
        fs_thread = std::thread(fileserver_thread, file_sd);
    }
    catch (const std::system_error &err) {
        LOG(llevError, "fileserver_init: failed to create thread, code %d, what %s\n", err.code().value(), err.what());
        return 0;
    }
    return 1;
}

void fileserver_exit() {
  if (fs_thread.joinable()) {
      if (write(pipe_sds[1], "x", 1) == -1) {
          LOG(llevError, "fileserver_exit: write failed, code %d, what %s\n", errno, strerror(errno));
      }
      fs_thread.join();
  }
}