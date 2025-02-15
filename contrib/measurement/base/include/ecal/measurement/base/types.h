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
 * @file   types.h
 * @brief  Type definitions for low level measurement access
**/

#pragma once 

#include <set>
#include <vector>

namespace eCAL
{
  namespace measurement
  {
    namespace base
    {
      /**
       * @brief Info struct for a single measurement entry
      **/
      struct EntryInfo
      {
        long long RcvTimestamp;   //!< Receive time stamp
        long long ID;             //!< Channel ID
        long long SndClock;       //!< Send clock
        long long SndTimestamp;   //!< Send time stamp
        long long SndID;          //!< Send ID

        //!< @cond
        EntryInfo() : RcvTimestamp(0), ID(0), SndClock(0), SndTimestamp(0), SndID(0) {}

        EntryInfo(long long rcv_timestamp, long long id) : RcvTimestamp(rcv_timestamp), ID(id), SndClock(0), SndTimestamp(0), SndID(0) {}

        EntryInfo(long long rcv_timestamp, long long id, long long snd_clock) : RcvTimestamp(rcv_timestamp), ID(id), SndClock(snd_clock), SndTimestamp(0), SndID(0) {}

        EntryInfo(long long rcv_timestamp, long long id, long long snd_clock, long long snd_timestamp) : RcvTimestamp(rcv_timestamp), ID(id), SndClock(snd_clock), SndTimestamp(snd_timestamp), SndID(0) {}

        EntryInfo(long long rcv_timestamp, long long id, long long snd_clock, long long snd_timestamp, long long snd_id) : RcvTimestamp(rcv_timestamp), ID(id), SndClock(snd_clock), SndTimestamp(snd_timestamp), SndID(snd_id) {}

        bool operator==(const EntryInfo& other) const
        {
          return (ID == other.ID && SndTimestamp == other.SndTimestamp && RcvTimestamp == other.RcvTimestamp && SndClock == other.SndClock && SndID == other.SndID);
        }

        bool operator<(const EntryInfo& other) const
        {
          return (RcvTimestamp < other.RcvTimestamp);
        }
        //!< @endcond
      };

      /**
       * @brief eCAL HDF5 entries (as set container)
      **/
      using EntryInfoSet = std::set<EntryInfo>;

      /**
       * @brief eCAL HDF5 entries (as vector container)
      **/
      using EntryInfoVect = std::vector<EntryInfo>;

      /**
       * @brief eCAL Measurement Access types
      **/
      enum AccessType
      {
        RDONLY,
        CREATE
      };

    }
  }
}