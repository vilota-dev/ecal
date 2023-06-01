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
 * @brief  eCAL registration provider
 * 
 * All process internal publisher/subscriber, server/clients register here with all their attributes.
 * 
 * These information will be send cyclic (registration refresh) via UDP to external eCAL processes.
 * 
**/

#include <ecal/ecal_config.h>

#include "ecal_def.h"
#include "ecal_globals.h"
#include "ecal_registration_provider.h"
#include "ecal_descgate.h"

#include "io/udp_configurations.h"
#include "io/snd_sample.h"

namespace eCAL
{
  extern eCAL_Process_eSeverity  g_process_severity;
  extern std::string             g_process_info;

  extern std::atomic<long long>  g_process_wbytes;
  extern std::atomic<long long>  g_process_wbytes_sum;

  extern std::atomic<long long>  g_process_rbytes;
  extern std::atomic<long long>  g_process_rbytes_sum;

  std::atomic<bool> CRegistrationProvider::m_created;

  CRegistrationProvider::CRegistrationProvider() :
                    m_multicast_group(NET_UDP_MULTICAST_GROUP),
                    m_reg_refresh(CMN_REGISTRATION_REFRESH),
                    m_reg_topics(false),
                    m_reg_services(false),
                    m_reg_process(false),
                    m_use_network_monitoring(false),
                    m_use_shm_monitoring(false)

  {
  }

  CRegistrationProvider::~CRegistrationProvider()
  {
    Destroy();
  }

  void CRegistrationProvider::Create(bool topics_, bool services_, bool process_)
  {
    if(m_created) return;

    m_reg_refresh     = Config::GetRegistrationRefreshMs();

    m_reg_topics      = topics_;
    m_reg_services    = services_;
    m_reg_process     = process_;

    m_use_shm_monitoring     = Config::Experimental::IsShmMonitoringEnabled();
    m_use_network_monitoring = !Config::Experimental::IsNetworkMonitoringDisabled();

    if (m_use_network_monitoring)
    {
      SSenderAttr attr;
      // for local only communication we switch to local broadcasting to bypass vpn's or firewalls
      attr.broadcast = !Config::IsNetworkEnabled();
      attr.ipaddr    = UDP::GetRegistrationMulticastAddress();
      attr.port      = Config::GetUdpMulticastPort() + NET_UDP_MULTICAST_PORT_REG_OFF;
      attr.loopback  = true;
      attr.ttl       = Config::GetUdpMulticastTtl();
      attr.sndbuf    = Config::GetUdpMulticastSndBufSizeBytes();

      m_multicast_group = attr.ipaddr;

      m_reg_snd.Create(attr);
    }
    else
    {
      std::cout << "Network monitoring is disabled" << std::endl;
    }

    if (m_use_shm_monitoring)
    {
      std::cout << "Shared memory monitoring is enabled (domain: " << Config::Experimental::GetShmMonitoringDomain() << " - queue size: " << Config::Experimental::GetShmMonitoringQueueSize() << ")" << std::endl;
      m_memfile_broadcast.Create(Config::Experimental::GetShmMonitoringDomain(), Config::Experimental::GetShmMonitoringQueueSize());
      m_memfile_broadcast_writer.Bind(&m_memfile_broadcast);
    }

    m_reg_snd_thread.Start(Config::GetRegistrationRefreshMs(), std::bind(&CRegistrationProvider::RegisterSendThread, this));

    m_created = true;
  }

  void CRegistrationProvider::Destroy()
  {
    if(!m_created) return;

    // this is our last (un)registration message to the world
    // thank you and goodbye :-)
    UnregisterProcess();

    m_reg_snd_thread.Stop();

    if(m_use_shm_monitoring)
    {
      m_memfile_broadcast_writer.Unbind();
      m_memfile_broadcast.Destroy();
    }

    m_created = false;
  }

  bool CRegistrationProvider::RegisterTopic(const std::string& topic_name_, const std::string& topic_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if(!m_created)    return(false);
    if(!m_reg_topics) return(false);

    const std::lock_guard<std::mutex> lock(m_topics_map_sync);
    m_topics_map[topic_name_ + topic_id_] = ecal_sample_;
    if(force_)
    {
      RegisterProcess();
      // apply registration sample
      ApplySample(topic_name_, ecal_sample_);
      SendSampleList(false);
    }

    return(true);
  }

  bool CRegistrationProvider::UnregisterTopic(const std::string& topic_name_, const std::string& topic_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if(!m_created) return(false);

    if (force_)
    {
      // apply unregistration sample
      ApplySample(topic_name_, ecal_sample_);
      SendSampleList(false);
    }

    SampleMapT::iterator iter;
    const std::lock_guard<std::mutex> lock(m_topics_map_sync);
    iter = m_topics_map.find(topic_name_ + topic_id_);
    if(iter != m_topics_map.end())
    {
      m_topics_map.erase(iter);
      return(true);
    }

    return(false);
  }

  bool CRegistrationProvider::RegisterServer(const std::string& service_name_, const std::string& service_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if(!m_created)      return(false);
    if(!m_reg_services) return(false);

    const std::lock_guard<std::mutex> lock(m_server_map_sync);
    m_server_map[service_name_ + service_id_] = ecal_sample_;
    if(force_)
    {
      RegisterProcess();
      // apply registration sample
      ApplySample(service_name_, ecal_sample_);
      SendSampleList(false);
    }

    return(true);
  }

  bool CRegistrationProvider::UnregisterServer(const std::string& service_name_, const std::string& service_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if(!m_created) return(false);

    if (force_)
    {
      // apply unregistration sample
      ApplySample(service_name_, ecal_sample_);
      SendSampleList(false);
    }

    SampleMapT::iterator iter;
    const std::lock_guard<std::mutex> lock(m_server_map_sync);
    iter = m_server_map.find(service_name_ + service_id_);
    if(iter != m_server_map.end())
    {
      m_server_map.erase(iter);
      return(true);
    }

    return(false);
  }

  bool CRegistrationProvider::RegisterClient(const std::string& client_name_, const std::string& client_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if (!m_created)      return(false);
    if (!m_reg_services) return(false);

    const std::lock_guard<std::mutex> lock(m_client_map_sync);
    m_client_map[client_name_ + client_id_] = ecal_sample_;
    if (force_)
    {
      RegisterProcess();
      // apply registration sample
      ApplySample(client_name_, ecal_sample_);
      SendSampleList(false);
    }

    return(true);
  }

  bool CRegistrationProvider::UnregisterClient(const std::string& client_name_, const std::string& client_id_, const eCAL::pb::Sample& ecal_sample_, const bool force_)
  {
    if (!m_created) return(false);

    if (force_)
    {
      // apply unregistration sample
      ApplySample(client_name_, ecal_sample_);
      SendSampleList(false);
    }

    SampleMapT::iterator iter;
    const std::lock_guard<std::mutex> lock(m_client_map_sync);
    iter = m_client_map.find(client_name_ + client_id_);
    if (iter != m_client_map.end())
    {
      m_client_map.erase(iter);
      return(true);
    }

    return(false);
  }

  bool CRegistrationProvider::RegisterProcess()
  {
    if(!m_created)     return(false);
    if(!m_reg_process) return(false);

    eCAL::pb::Sample process_sample;
    process_sample.set_cmd_type(eCAL::pb::bct_reg_process);
    auto *process_sample_mutable_process = process_sample.mutable_process();
    process_sample_mutable_process->set_hname(Process::GetHostName());
    process_sample_mutable_process->set_pid(Process::GetProcessID());
    process_sample_mutable_process->set_pname(Process::GetProcessName());
    process_sample_mutable_process->set_uname(Process::GetUnitName());
    process_sample_mutable_process->set_pparam(Process::GetProcessParameter());
    process_sample_mutable_process->set_pmemory(Process::GetProcessMemory());
    process_sample_mutable_process->set_pcpu(Process::GetProcessCpuUsage());
    process_sample_mutable_process->set_usrptime(static_cast<float>(Logging::GetCoreTime()));
    process_sample_mutable_process->set_datawrite(google::protobuf::int64(Process::GetWBytes()));
    process_sample_mutable_process->set_dataread(google::protobuf::int64(Process::GetRBytes()));
    process_sample_mutable_process->mutable_state()->set_severity(eCAL::pb::eProcessSeverity(g_process_severity));
    process_sample_mutable_process->mutable_state()->set_info(g_process_info);
    if (g_timegate() == nullptr)
    {
      process_sample_mutable_process->set_tsync_state(eCAL::pb::eTSyncState::tsync_none);
    }
    else
    {
      if (!g_timegate()->IsSynchronized())
      {
        process_sample_mutable_process->set_tsync_state(eCAL::pb::eTSyncState::tsync_none);
      }
      else
      {
        switch (g_timegate()->GetSyncMode())
        {
        case CTimeGate::eTimeSyncMode::realtime:
          process_sample_mutable_process->set_tsync_state(eCAL::pb::eTSyncState::tsync_realtime);
          break;
        case CTimeGate::eTimeSyncMode::replay:
          process_sample_mutable_process->set_tsync_state(eCAL::pb::eTSyncState::tsync_replay);
          break;
        default:
          process_sample_mutable_process->set_tsync_state(eCAL::pb::eTSyncState::tsync_none);
          break;
        }
      }
      process_sample_mutable_process->set_tsync_mod_name(g_timegate()->GetName());
    }

    // eCAL initialization state
    const unsigned int comp_state(g_globals()->GetComponents());
    process_sample_mutable_process->set_component_init_state(google::protobuf::int32(comp_state));
    std::string component_info;
    if ((comp_state & Init::Publisher)  != 0u) component_info += "|pub";
    if ((comp_state & Init::Subscriber) != 0u) component_info += "|sub";
    if ((comp_state & Init::Service)    != 0u) component_info += "|srv";
    if ((comp_state & Init::Monitoring) != 0u) component_info += "|mon";
    if ((comp_state & Init::Logging)    != 0u) component_info += "|log";
    if ((comp_state & Init::TimeSync)   != 0u) component_info += "|time";
    if (!component_info.empty()) component_info = component_info.substr(1);
    process_sample_mutable_process->set_component_init_info(component_info);

    process_sample_mutable_process->set_ecal_runtime_version(eCAL::GetVersionString());

    // apply registration sample
    const bool return_value = ApplySample(Process::GetHostName(), process_sample);

    return return_value;
  }

  bool CRegistrationProvider::UnregisterProcess()
  {
	  if (!m_created)     return(false);
	  if (!m_reg_process) return(false);

	  eCAL::pb::Sample process_sample;
	  process_sample.set_cmd_type(eCAL::pb::bct_unreg_process);
	  auto* process_sample_mutable_process = process_sample.mutable_process();
	  process_sample_mutable_process->set_hname(Process::GetHostName());
	  process_sample_mutable_process->set_pid(Process::GetProcessID());
	  process_sample_mutable_process->set_pname(Process::GetProcessName());
	  process_sample_mutable_process->set_uname(Process::GetUnitName());

    // apply unregistration sample
    const bool return_value = ApplySample(Process::GetHostName(), process_sample);

	  return return_value;
  }

  bool CRegistrationProvider::RegisterServer()
  {
    if(!m_created)      return(false);
    if(!m_reg_services) return(false);

    bool return_value {true};
    const std::lock_guard<std::mutex> lock(m_server_map_sync);
    for(SampleMapT::const_iterator iter = m_server_map.begin(); iter != m_server_map.end(); ++iter)
    {
      //////////////////////////////////////////////
      // update description
      //////////////////////////////////////////////
      const auto& ecal_sample_service = iter->second.service();
      for (const auto& method : ecal_sample_service.methods())
      {
        ApplyServiceToDescGate(ecal_sample_service.sname(), method.mname(), method.req_type(), method.req_desc(), method.resp_type(), method.resp_desc());
      }

      //////////////////////////////////////////////
      // send sample to registration layer
      //////////////////////////////////////////////
      return_value &= ApplySample(iter->second.service().sname(), iter->second);
    }

    return return_value;
  }

  bool CRegistrationProvider::RegisterClient()
  {
    if (!m_created)      return(false);
    if (!m_reg_services) return(false);

    bool return_value {true};
    const std::lock_guard<std::mutex> lock(m_client_map_sync);
    for (SampleMapT::const_iterator iter = m_client_map.begin(); iter != m_client_map.end(); ++iter)
    {
      // apply registration sample
      return_value &= ApplySample(iter->second.client().sname(), iter->second);
    }

    return return_value;
  }

  bool CRegistrationProvider::RegisterTopics()
  {
    if(!m_created)    return(false);
    if(!m_reg_topics) return(false);

    bool return_value {true};
    const std::lock_guard<std::mutex> lock(m_topics_map_sync);
    for(SampleMapT::const_iterator iter = m_topics_map.begin(); iter != m_topics_map.end(); ++iter)
    {
      //////////////////////////////////////////////
      // update description
      //////////////////////////////////////////////
      // read attributes
      const std::string topic_name(iter->second.topic().tname());
      STopicInformation topic_info;
      auto& pb_topic_info = iter->second.topic().tinfo();
      topic_info.encoding = pb_topic_info.encoding();
      topic_info.type = pb_topic_info.type();
      topic_info.descriptor = pb_topic_info.desc();
      const bool        topic_is_a_publisher(iter->second.cmd_type() == eCAL::pb::eCmdType::bct_reg_publisher);
      ApplyTopicToDescGate(topic_name, topic_info, topic_is_a_publisher);

      //////////////////////////////////////////////
      // send sample to registration layer
      //////////////////////////////////////////////
      return_value &= ApplySample(iter->second.topic().tname(), iter->second);
    }

    return return_value;
  }

  bool CRegistrationProvider::ApplySample(const std::string& sample_name_, const eCAL::pb::Sample& sample_)
  {
    if(!m_created) return(false);

    bool return_value {true};

    if(m_use_network_monitoring)
      return_value &= (SendSample(&m_reg_snd, sample_name_, sample_, m_multicast_group, -1) != 0);

    if(m_use_shm_monitoring)
    {
      const std::lock_guard<std::mutex> lock(m_sample_list_sync);
      m_sample_list.mutable_samples()->Add()->CopyFrom(sample_);
    }

    return return_value;
  }

  bool CRegistrationProvider::SendSampleList(bool reset_sample_list_)
  {
    if(!m_created) return(false);
    bool return_value {true};

    if(m_use_shm_monitoring)
    {
      {
        const std::lock_guard<std::mutex> lock(m_sample_list_sync);
        m_sample_list.SerializeToString(&m_sample_list_buffer);
        if(reset_sample_list_)
          m_sample_list.clear_samples();
      }

      if(!m_sample_list_buffer.empty())
        return_value &=m_memfile_broadcast_writer.Write(m_sample_list_buffer.data(), m_sample_list_buffer.size());
    }

    return return_value;
  }

  int CRegistrationProvider::RegisterSendThread()
  {
    if(!m_created) return(0);

    // calculate average receive bytes
    g_process_rbytes = static_cast<long long>(((double)g_process_rbytes_sum / m_reg_refresh)*1000.0);
    g_process_rbytes_sum = 0;

    // calculate average write bytes
    g_process_wbytes = static_cast<long long>(((double)g_process_wbytes_sum / m_reg_refresh)*1000.0);
    g_process_wbytes_sum = 0;

    // refresh subscriber registration
    if (g_subgate() != nullptr) g_subgate()->RefreshRegistrations();

    // refresh publisher registration
    if (g_pubgate() != nullptr) g_pubgate()->RefreshRegistrations();

    // refresh server registration
    if (g_servicegate() != nullptr) g_servicegate()->RefreshRegistrations();

    // refresh client registration
    if (g_clientgate() != nullptr) g_clientgate()->RefreshRegistrations();

    // register process
    RegisterProcess();

    // register server
    RegisterServer();

    // register clients
    RegisterClient();

    // register topics
    RegisterTopics();

    // write sample list to shared memory
    SendSampleList();

    return(0);
  }

  bool CRegistrationProvider::ApplyTopicToDescGate(const std::string& topic_name_
    , const STopicInformation& topic_info_
    , bool topic_is_a_publisher_)
  {
    if (g_descgate() != nullptr)
    {
      // calculate the quality of the current info
      ::eCAL::CDescGate::QualityFlags quality = ::eCAL::CDescGate::QualityFlags::NO_QUALITY;
      if (!topic_info_.encoding.empty() || !topic_info_.type.empty())
        quality |= ::eCAL::CDescGate::QualityFlags::TYPE_AVAILABLE;
      if (!topic_info_.descriptor.empty())
        quality |= ::eCAL::CDescGate::QualityFlags::DESCRIPTION_AVAILABLE;
      if (topic_is_a_publisher_)
        quality |= ::eCAL::CDescGate::QualityFlags::INFO_COMES_FROM_PRODUCER;
      quality |= ::eCAL::CDescGate::QualityFlags::INFO_COMES_FROM_THIS_PROCESS;
      quality |= ::eCAL::CDescGate::QualityFlags::INFO_COMES_FROM_CORRECT_ENTITY;
      // update description
      return g_descgate()->ApplyTopicDescription(topic_name_, topic_info_, quality);
    }
    return false;
  }

  bool CRegistrationProvider::ApplyServiceToDescGate(const std::string& service_name_
    , const std::string& method_name_
    , const std::string& req_type_name_
    , const std::string& req_type_desc_
    , const std::string& resp_type_name_
    , const std::string& resp_type_desc_)
  {
    if (g_descgate() != nullptr)
    {
      // Calculate the quality of the current info
      ::eCAL::CDescGate::QualityFlags quality = ::eCAL::CDescGate::QualityFlags::NO_QUALITY;
      if (!(req_type_name_.empty() && resp_type_name_.empty()))
        quality |= ::eCAL::CDescGate::QualityFlags::TYPE_AVAILABLE;
      if (!(req_type_desc_.empty() && resp_type_desc_.empty()))
        quality |= ::eCAL::CDescGate::QualityFlags::DESCRIPTION_AVAILABLE;
      quality |= ::eCAL::CDescGate::QualityFlags::INFO_COMES_FROM_THIS_PROCESS;

      return g_descgate()->ApplyServiceDescription(service_name_, method_name_, req_type_name_, req_type_desc_, resp_type_name_, resp_type_desc_, quality);
    }
    return false;
  }
}
