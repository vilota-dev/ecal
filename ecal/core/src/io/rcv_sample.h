/* ========================= eCAL LICENSE =================================
 *
 * Copyright (C) 2016 - 2019 Continental Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ========================= eCAL LICENSE =================================
*/

/**
 * @brief  Receiver thread for ecal samples
**/

#pragma once

#include "ecal_def.h"
#include "udp_receiver.h"
#include "ecal_thread.h"
#include "msg_type.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <chrono>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4127 4146 4505 4800 4189 4592) // disable proto warnings
#endif
#include <ecal/core/pb/ecal.pb.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

class CReceiveSlot
{
public:
  CReceiveSlot();
  virtual ~CReceiveSlot();

  int ApplyMessage(const struct SUDPMessage& ecal_message_);
  bool HasFinished() {return((m_recv_mode == rcm_aborted) || (m_recv_mode == rcm_completed));};

  // For logging statistics
  bool HasAborted() {return(m_recv_mode == rcm_aborted);};
  bool HasCompleted() {return(m_recv_mode == rcm_completed);};

  bool HasTimedOut(const std::chrono::duration<double>& diff_time_) {m_timeout += diff_time_; return(m_timeout >= std::chrono::milliseconds(NET_UDP_RECBUFFER_TIMEOUT));};
  int32_t GetMessageTotalLength() {return(m_message_total_len);};
  int32_t GetMessageCurrentLength() {return(m_message_curr_len);};

  virtual int OnMessageCompleted(std::vector<char> &&msg_buffer_) = 0;

  int32_t           m_message_id;

protected:
  int OnMessageStart(const struct SUDPMessage& ecal_message_);
  int OnMessageData(const struct SUDPMessage& ecal_message_);

  enum eReceiveMode
  {
    rcm_waiting = 1,
    rcm_reading,
    rcm_aborted,
    rcm_completed
  };

  std::chrono::duration<double> m_timeout;
  std::vector<char> m_recv_buffer;
  eReceiveMode      m_recv_mode;

  int32_t           m_message_total_num;
  int32_t           m_message_total_len;

  int32_t           m_message_curr_num;
  int32_t           m_message_curr_len;

  eCAL::pb::Sample    m_ecal_sample;
};

/**
 * @brief Struct for storing statistics for each sample for each ID
 * Keeps track of the time point the header received, and the callbacks will check if 1 second has passed
 * since the first message was received. Will be printed out once 1 second mark has passed, and is cleared from the
 * m_stats_map afterwards. Only the received param will change. The rest will remain the same from construction.
 *
 * @param received          Number of received messages
 * @param total             Total number of messages (from the header message)
 * @param slot              Shared pointer to the receive slot
 * @param m_first_received  Time point of the first received message
 */
struct SampleStats {
  int received;
  int total;
  std::shared_ptr<CReceiveSlot> slot;
  std::chrono::steady_clock::time_point m_first_received;
};

class CSampleReceiver
{
  class CSampleReceiveSlot : public CReceiveSlot
  {
  public:
    explicit CSampleReceiveSlot(CSampleReceiver* sample_receiver_);
    virtual ~CSampleReceiveSlot();

    virtual int OnMessageCompleted(std::vector<char> &&msg_buffer_);

  protected:
    CSampleReceiver* m_sample_receiver;
  };

public:
  CSampleReceiver();
  virtual ~CSampleReceiver();

  virtual bool HasSample(const std::string& sample_name_)                                        = 0;
  virtual size_t ApplySample(const eCAL::pb::Sample& ecal_sample_, eCAL::pb::eTLayerType layer_) = 0;

  int Receive(eCAL::CUDPReceiver* sample_receiver_);
  int Process(const char* sample_buffer_, size_t sample_buffer_len_);

protected:
  typedef std::unordered_map<int32_t, std::shared_ptr<CSampleReceiveSlot>> ReceiveSlotMapT;
  ReceiveSlotMapT    m_receive_slot_map;
  std::vector<char>  m_msg_buffer;
  eCAL::pb::Sample     m_ecal_sample;

  std::chrono::steady_clock::time_point m_cleanup_start;

  // Store information for each message id
  typedef std::unordered_map<int32_t, std::shared_ptr<SampleStats>> StatsMapT;
  StatsMapT m_stats_map;
};
