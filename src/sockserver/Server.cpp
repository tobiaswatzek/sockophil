#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <memory>
#include <cstring>
#include <iostream>
#include <sstream>
#include <dirent.h>
#include <algorithm>
#include <fstream>
#include "sockserver/Server.h"
#include "sockophil/Helper.h"
#include "cereal/archives/portable_binary.hpp"
#include "sockophil/ErrnoExceptions.h"
#include "sockophil/ListPackage.h"
#include "sockophil/Package.h"
#include "sockophil/DataPackage.h"
#include "sockophil/ActionPackage.h"
#include "sockophil/ErrorPackage.h"
#include "sockophil/Constants.h"

namespace sockserver {

/**
 * @brief Constructor that stores member vars and sets up the socket
 * @param port is the port that should be listend on
 * @param target_directory is the directory to upload files
 */
Server::Server(unsigned short port, std::string target_directory) : port(port) {
  this->target_directory = sockophil::Helper::add_trailing_slash(target_directory);
  this->dir_list();
  this->create_socket();
  this->bind_to_socket();
}
/**
 * @brief Destructor closes the socket
 */
Server::~Server() {
  this->close_socket();
}

/**
 * @brief Try to create a socket
 */
void Server::create_socket() {
  this->socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (this->socket_descriptor == -1) {
    throw sockophil::SocketCreationException(errno);
  }
  /* set the whole struct to 0's */
  std::memset(&this->server_address, 0, sizeof(this->server_address));
  /* telling the server address all values will be IP addresses */
  this->server_address.sin_family = AF_INET;
  /* dont know yet */
  this->server_address.sin_addr.s_addr = INADDR_ANY;
  /* telling the server address the port number given to our object and
   * putting it through htons (host to network short) just in case
   */
  this->server_address.sin_port = htons(this->port);

}

/**
 * @brief Bind to the socket
 */
void Server::bind_to_socket() {
  if (bind(this->socket_descriptor, (struct sockaddr *) &this->server_address, sizeof(server_address)) != 0) {
    throw sockophil::SocketBindException(errno);
  }
}

/**
 * @brief Listen to the socket in an endless loop
 */
void Server::listen_on_socket() {

  if (listen(this->socket_descriptor, 5) < 0) {
    throw sockophil::SocketListenException(errno);
  }
  bool keep_listening = true;
  socklen_t client_address_length = sizeof(struct sockaddr_in);
  int accepted_socket = -1;
  while (keep_listening) {
    std::cout << "Waiting for clients..." << std::endl;
    /* we start listening */
    if (accepted_socket < 0) {
      accepted_socket = accept(this->socket_descriptor, (struct sockaddr *) &this->client_address,
                               &client_address_length);
      if (accepted_socket < 0) {
        throw sockophil::SocketAcceptException(errno);
      } else {
        std::cout << "Client connected from " << inet_ntoa(this->client_address.sin_addr) << ": "
                  << ntohs(this->client_address.sin_port) << std::endl;
      }
    }

    auto received_pkg = this->receive_package(accepted_socket);
    std::cout << received_pkg->get_type() << std::endl;
    if (received_pkg->get_type() == sockophil::ACTION_PACKAGE) {
      switch (std::static_pointer_cast<sockophil::ActionPackage>(received_pkg)->get_action()) {
        case sockophil::LIST:
          this->send_package(accepted_socket, std::make_shared<sockophil::ListPackage>(this->dir_list()));
          break;
        case sockophil::PUT:
          this->store_file(accepted_socket);
          break;
        case sockophil::GET:
          this->return_file(accepted_socket,
                            std::static_pointer_cast<sockophil::ActionPackage>(received_pkg)
                                ->get_filename());
          break;
        case sockophil::QUIT:
          close(accepted_socket);
          //keep_listening = false;
          break;
      }
    }
  }
}

void Server::close_socket() {
  close(this->socket_descriptor);
}

void Server::run() {
  this->listen_on_socket();
}

std::vector<std::string> Server::dir_list() const {
  DIR *dirptr;
  bool check = true;
  struct dirent *direntry;
  std::vector<std::string> list;
  dirptr = opendir(this->target_directory.c_str());
  if (dirptr != NULL) {
    while (check) {
      direntry = readdir(dirptr);
      if (direntry) {
        list.push_back(std::string(direntry->d_name));
      } else {
        check = false;
      }
    }
    std::sort(list.begin(), list.end());
    /* erase first 2 entries (".","..") */
    list.erase(list.begin(), list.begin() + 2);
  }
  closedir(dirptr);
  return list;
}

void Server::store_file(int accepted_socket) {
  auto received_pkg = this->receive_package(accepted_socket);
  std::shared_ptr<sockophil::Package> pkg = nullptr;
  if (received_pkg->get_type() == sockophil::DATA_PACKAGE) {
    auto data_pkg = std::static_pointer_cast<sockophil::DataPackage>(received_pkg);
    std::ofstream output_file;
    output_file.open(this->target_directory + data_pkg->get_filename(), std::ios::out | std::ios::binary);
    if (output_file.is_open()) {
      output_file.write((char *) data_pkg->get_data_raw().data(), data_pkg->get_data_raw().size());
      pkg = std::make_shared<sockophil::SuccessPackage>();
    } else {
      pkg = std::make_shared<sockophil::ErrorPackage>(sockophil::FILE_STORAGE);
    }
    output_file.close();
  } else {
    pkg = std::make_shared<sockophil::ErrorPackage>(sockophil::WRONG_PACKAGE);
  }
  this->send_package(accepted_socket, pkg);
}

void Server::return_file(int accepted_socket, std::string filename) {
  std::string filepath = this->target_directory + filename;
  std::vector<uint8_t> content;
  std::ifstream in_file;
  std::shared_ptr<sockophil::Package> pkg = nullptr;
  in_file.open(filepath, std::ios::in | std::ios::binary);
  if (in_file.is_open()) {
    std::for_each(std::istreambuf_iterator<char>(in_file),
                  std::istreambuf_iterator<char>(),
                  [&content](const char c) {
                    content.push_back(c);
                  });
    pkg = std::make_shared<sockophil::DataPackage>(content, filename);
  } else {
    pkg = std::make_shared<sockophil::ErrorPackage>(sockophil::FILE_NOT_FOUND);
  }
  this->send_package(accepted_socket, pkg);
}
}