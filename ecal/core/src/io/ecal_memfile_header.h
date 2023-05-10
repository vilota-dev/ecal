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
 * @brief  eCAL common message format
**/

#pragma once

#include <stdint.h>

namespace eCAL
{
  struct SMemFileHeader
  { //-V802
    std::uint16_t   hdr_size   = sizeof(SMemFileHeader);
    std::uint64_t   data_size  = 0;
    std::uint64_t   id         = 0;
    std::uint64_t   clock      = 0;
    std::int64_t    time       = 0;
    std::uint64_t   hash       = 0;
    // ----- > 5.8 -----
    struct optflags
    {
      unsigned char zero_copy : 1;    // allow reader to access memory without copying
      unsigned char unused    : 7;
    };
    optflags        options = { 0, 0 };
    // ----- > 5.11 ----
    std::int64_t    ack_timout_ms = 0;
  };
}
 