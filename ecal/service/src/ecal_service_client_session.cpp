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

#include <ecal/service/ecal_service_client_session.h>

#include "ecal_service_client_session_impl_v1.h"
#include "condition_variable_signaler.h"

namespace eCAL
{
  namespace service
  {
    std::shared_ptr<ClientSession> ClientSession::create(asio::io_context& io_context
                                                  , std::uint8_t           protocol_version
                                                  , const std::string&     address
                                                  , std::uint16_t          port
                                                  , const EventCallbackT&  event_callback
                                                  , const LoggerT&         logger)
    {
      return std::shared_ptr<ClientSession>(new ClientSession(io_context, protocol_version, address, port, event_callback, logger));
    }

    ClientSession::ClientSession(asio::io_context&      io_context
                                , std::uint8_t          protocol_version
                                , const std::string&    address
                                , std::uint16_t         port
                                , const EventCallbackT& event_callback
                                , const LoggerT&        logger)
    {
      if (protocol_version == 0)
      {
        // TODO: Implement
      }
      else
      {
        impl_ = ClientSessionV1::create(io_context, address, port, event_callback, logger);
      }
    }

    ClientSession::~ClientSession()
    {
      impl_->stop();
    }

    //////////////////////////////////////////////
    // Public API
    //////////////////////////////////////////////
    void ClientSession::async_call_service(const std::shared_ptr<const std::string>& request, const ResponseCallbackT& response_callback)
    {
      return impl_->async_call_service(request, response_callback);
    }

    eCAL::service::Error ClientSession::call_service(const std::shared_ptr<const std::string>& request, std::shared_ptr<std::string>& response)
    {
      eCAL::service::Error error(Error::GENERIC_ERROR);
        
      // Create a condition variable and a mutex to wait for the response
      std::mutex              mutex;
      std::condition_variable condition_variable;
      bool                    is_signaled = false;
      
      {
        // Create a response callback, that will set the response and notify the condition variable
        ResponseCallbackT response_callback
                  //= [&error, &response, &mutex, &condition_variable]
                  = [&error, &response, signaler = std::make_shared<ConditionVariableSignaler>(condition_variable, mutex, is_signaled)]
                    (const eCAL::service::Error& response_error, const std::shared_ptr<std::string>& response_)
                    {
                      response = response_;
                      error    = response_error;

                      //{
                      //  std::lock_guard<std::mutex> lock(mutex);
                      //  condition_variable.notify_all();
                      //}
                    };
        async_call_service(request, response_callback); 
      }

      // Lock mutex, call service asynchronously and wait for the condition variable to be notified
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition_variable.wait(lock, [&is_signaled]() { return is_signaled; });
      }

      return error;
    }

  } // namespace service
} // namespace eCAL
