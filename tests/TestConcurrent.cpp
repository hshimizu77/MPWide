/**************************************************************
 * This file is part of the MPWide communication library
 *
 * Written by Derek Groen with thanks going out to Steven Rieder,
 * Simon Portegies Zwart, Joris Borgdorff, Hans Blom and Tomoaki Ishiyama.
 * for questions, please send an e-mail to: 
 *                                     djgroennl@gmail.com
 * MPWide is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published 
 * by the Free Software Foundation, either version 3 of the License, 
 * or (at your option) any later version.
 *
 * MPWide is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with MPWide.  If not, see <http://www.gnu.org/licenses/>.
 * **************************************************************/

#include <iostream>
#include <fstream>
#include <cstdlib>

#include <cmath>
#include <iostream>

#include <stack>
#include <cstring>
#include <errno.h>

#include <thread>
#include <mutex>
#include <future>

#include <winsock2.h>
#include <ws2tcpip.h>


using namespace std;

#include "../MPWide.h"

/*
  TestConcurrent.cpp
  A comprehensive test of socket connects and MPW_SendRecv using one process with two threads.
*/


std::mutex log_mutex;
std::mutex path_mutex;

#define LOG(X) { log_mutex.lock(); cout << X << endl; log_mutex.unlock(); }

struct vars
{
  int num_channels_or_path_id;
  size_t msg_size;
  bool do_send;
};

int do_connect(string host, int port, int num_channels, bool asServer)
{
  path_mutex.lock();
  int path_id = MPW_CreatePathWithoutConnect(host, port, num_channels); ///path version
  path_mutex.unlock();

  if (path_id >= 0) {
    LOG("Connecting to path " << path_id << "; server: " << asServer);
    
    if (MPW_ConnectPath(path_id, asServer) < 0) {
      path_mutex.lock();
      MPW_DestroyPath(path_id);
      path_mutex.unlock();
      path_id = -1;
    }
    
  }
  else LOG("Failed to connecting to path " << path_id << "; server: " << asServer);

  
  return path_id;
}

int *server_thread(void * data)
{
  int num_channels = ((vars *)data)->num_channels_or_path_id;
  int *path_id = new int;
  LOG("Server thread accepting connections");
  *path_id = do_connect("0", 16256, num_channels, true);

  return path_id;
}

int *connecting_thread(void *data)
{
  int num_channels = ((vars *)data)->num_channels_or_path_id;
  size_t msg_size = ((vars *)data)->msg_size;

  // Try to connect until you succeed
  int path_id = do_connect("localhost", 16256, num_channels, false);
  while (path_id < 0) {
    // Sleep 1.0 seconds
    Sleep(1000);
    path_id = do_connect("localhost", 16256, num_channels, false);
  }

  Sleep(1000);
  if (path_id >= 0) {
    LOG("Succesfully connected to server");
    if (((vars *)data)->do_send) {
      char *buf = new char[msg_size];
      memset(buf, 0, msg_size);
    
      int res;
      for (int i = 0; i < 20; i++) {
        LOG("Starting iteration " << i << " of connecting thread; path " << path_id);
        
        res = -MPW_SendRecv(0, 0, buf, msg_size, path_id);
        if (res > 0) LOG("Error receiving in connecting thread: " << strerror(res) << "/" << res);
        Sleep(1);

        res = -MPW_SendRecv(buf, msg_size, 0, 0, path_id);
        if (res > 0) LOG("Error sending in connecting thread: " << strerror(res) << "/" << res);
        Sleep(1);
      }
      
      delete [] buf;
    }
  }
  
  int *res = new int(path_id);
  return res;
}

void *communicating_thread(void	*data)
{
  int path_id = ((vars *)data)->num_channels_or_path_id;
  size_t msg_size = ((vars *)data)->msg_size;
  bool do_send = ((vars *)data)->do_send;
  
  char *buf = new char[msg_size];
  memset(buf, 0, msg_size);
  int res;
  
  LOG("Starting Blocking Communication Tests.");

  for (int i = 0; i < 10; i++) {
    if (do_send) {
      LOG("Starting iteration " << i << " of sending thread; path " << path_id);
      res = -MPW_SendRecv(buf, msg_size, 0, 0, path_id);
      if (res > 0) LOG("Error sending in sending thread: " << strerror(res) << "/" << res);
    } else {
      LOG("Starting iteration " << i << " of receiving thread; path " << path_id);
      res = -MPW_SendRecv(0, 0, buf, msg_size, path_id);
      if (res > 0) LOG("Error receiving in receiving thread: " << strerror(res) << "/" << res);
    }
  }
  
  LOG("Starting Non-Blocking Communication Tests.");
  for (int i = 0; i < 10; i++) {
    if (do_send) {
      int id = MPW_ISendRecv(buf, msg_size, 0, 0, path_id);
  
      Sleep(1000);

      cout << "Has the non-blocking comm finished?" << endl;
      MPW_Has_NBE_Finished(id);
      cout << "Doing something else..." << endl;

      MPW_Wait(id);
    } else {
      int id = MPW_ISendRecv(0, 0, buf, msg_size, path_id);

      Sleep(1000);
  
      cout << "Has the non-blocking comm finished?" << endl;
      MPW_Has_NBE_Finished(id);
      cout << "Doing something else..." << endl;

      MPW_Wait(id);
    }
  }
  delete [] buf;

  return NULL;
}

int main(int argc, char** argv){

  //  printf("usage: ./MPWConcurrentTest <channels (default: 1)> [<message size [kB] (default: 8 kB))>]\n");
  //  exit(EXIT_FAILURE);

  //Initialize Winsock 
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

  /* Initialize */
  int num_channels = (argc>1) ? atoi(argv[1]) : 1;
  size_t msgsize = (argc>2) ? atoi(argv[2])*1024 : 8*1024;

  vars v = {num_channels, msgsize, true};
  
  std::thread server_t, connect_t, send_t, recv_t;
  
  // Start accepting connections
  std::future<int*> accepted_path_future = std::async(launch::async, server_thread, &v);

  // Connect to server after giving it some time to start
  std::future<int*> connected_path_future = std::async(launch::async, connecting_thread, &v);

  // Accept the connection of the server
  int *accepted_path_id;
  int *connected_path_id;

  accepted_path_id = accepted_path_future.get();
  
  // Failed until shown otherwise
  int exit_value = EXIT_FAILURE;
  
  if (*accepted_path_id >= 0) {    
    // Start accepting connections again

	accepted_path_future = std::async(launch::async, server_thread, &v);

    vars sendv = {*accepted_path_id, msgsize, true};
    vars recvv = {*accepted_path_id, msgsize, false};
    delete accepted_path_id;
    
	send_t = std::thread(communicating_thread, &sendv);
    recv_t = std::thread(communicating_thread, &recvv);

    send_t.join();
    recv_t.join();

	connected_path_id = connected_path_future.get();

	delete connected_path_id;
    
    vars connectv = {num_channels, msgsize, false};

	//connect_t = std::thread(connecting_thread, &connectv);
	connected_path_future = std::async(launch::async, connecting_thread, &connectv);

	accepted_path_id = accepted_path_future.get();

    if (*accepted_path_id >= 0)
      exit_value = EXIT_SUCCESS;
  }

  delete accepted_path_id;

  //connect_t.join();
  connected_path_id = connected_path_future.get();

  delete connected_path_id;
    
  MPW_Finalize();
  WSACleanup();

  cout << "MPWTestConcurrent completed successfully." << endl;

  return exit_value;
}
