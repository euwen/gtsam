/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation, 
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    FixedLagSmoother.cpp
 * @brief   The base class for different fixed-lag smoother implementations.
 *
 * @author  Stephen Williams
 * @date    Feb 27, 2013
 */

#include <gtsam_unstable/nonlinear/FixedLagSmoother.h>

namespace gtsam {

/* ************************************************************************* */
void FixedLagSmoother::print(const std::string& s, const KeyFormatter& keyFormatter) const {
  std::cout << s;
  std::cout << "  smoother lag: " << smootherLag_ << std::endl;
}

/* ************************************************************************* */
bool FixedLagSmoother::equals(const FixedLagSmoother& rhs, double tol) const {
  return std::fabs(smootherLag_ - rhs.smootherLag_) < tol
      && std::equal(timestampKeyMap_.begin(), timestampKeyMap_.end(), rhs.timestampKeyMap_.begin());
}

/* ************************************************************************* */
void FixedLagSmoother::updateKeyTimestampMap(const KeyTimestampMap& timestamps) {
  // Loop through each key and add/update it in the map
  for(const KeyTimestampMap::value_type& key_timestamp: timestamps) {
    // Check to see if this key already exists in the database
    KeyTimestampMap::iterator keyIter = keyTimestampMap_.find(key_timestamp.first);

    // If the key already exists
    if(keyIter != keyTimestampMap_.end()) {
      // Find the entry in the Timestamp-Key database
      std::pair<TimestampKeyMap::iterator,TimestampKeyMap::iterator> range = timestampKeyMap_.equal_range(keyIter->second);
      TimestampKeyMap::iterator timeIter = range.first;
      while(timeIter->second != key_timestamp.first) {
        ++timeIter;
      }
      // remove the entry in the Timestamp-Key database
      timestampKeyMap_.erase(timeIter);
      // insert an entry at the new time
      timestampKeyMap_.insert(TimestampKeyMap::value_type(key_timestamp.second, key_timestamp.first));
      // update the Key-Timestamp database
      keyIter->second = key_timestamp.second;
    } else {
      // Add the Key-Timestamp database
      keyTimestampMap_.insert(key_timestamp);
      // Add the key to the Timestamp-Key database
      timestampKeyMap_.insert(TimestampKeyMap::value_type(key_timestamp.second, key_timestamp.first));
    }
  }
}

/* ************************************************************************* */
void FixedLagSmoother::eraseKeyTimestampMap(const std::set<Key>& keys) {
  for(Key key: keys) {
    // Erase the key from the Timestamp->Key map
    double timestamp = keyTimestampMap_.at(key);

    TimestampKeyMap::iterator iter = timestampKeyMap_.lower_bound(timestamp);
    while(iter != timestampKeyMap_.end() && iter->first == timestamp) {
      if(iter->second == key) {
        timestampKeyMap_.erase(iter++);
      } else {
        ++iter;
      }
    }
    // Erase the key from the Key->Timestamp map
    keyTimestampMap_.erase(key);
  }
}

/* ************************************************************************* */
double FixedLagSmoother::getCurrentTimestamp() const {
  if(timestampKeyMap_.size() > 0) {
    return timestampKeyMap_.rbegin()->first;
  } else {
    return -std::numeric_limits<double>::max();
  }
}

/* ************************************************************************* */
std::set<Key> FixedLagSmoother::findKeysBefore(double timestamp) const {
  std::set<Key> keys;
  TimestampKeyMap::const_iterator end = timestampKeyMap_.lower_bound(timestamp);
  for(TimestampKeyMap::const_iterator iter = timestampKeyMap_.begin(); iter != end; ++iter) {
    keys.insert(iter->second);
  }
  return keys;
}

/* ************************************************************************* */
std::set<Key> FixedLagSmoother::findKeysAfter(double timestamp) const {
  std::set<Key> keys;
  TimestampKeyMap::const_iterator begin = timestampKeyMap_.upper_bound(timestamp);
  for(TimestampKeyMap::const_iterator iter = begin; iter != timestampKeyMap_.end(); ++iter) {
    keys.insert(iter->second);
  }
  return keys;
}

/* ************************************************************************* */
} /// namespace gtsam
