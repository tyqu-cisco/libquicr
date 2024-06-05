#include "quicr/moq_messages.h"
#include "quicr/message_buffer.h"
#include "quicr/encode.h"

namespace quicr::messages {

//
// Utility
//
bool parse_uintV_field(qtransport::StreamBuffer<uint8_t> &buffer, uint64_t& field) {
  auto val = buffer.decode_uintV();
  if (!val) {
    return false;
  }
  field = val.value();
  return true;
}


bool parse_bytes_field(qtransport::StreamBuffer<uint8_t> &buffer, quicr::bytes& field) {
  auto val = buffer.decode_bytes();
  if (!val) {
    return false;
  }
  field = std::move(val.value());
  return true;
}

//
// Optional
//

template <typename T>
MessageBuffer& operator<<(MessageBuffer& buffer, const std::optional<T>& val) {
  if (val.has_value()) {
    buffer << val.value();
  }
  return buffer;
}

template <typename T>
MessageBuffer& operator>>(MessageBuffer& buffer, std::optional<T>& val) {
  T val_in{};
  buffer >> val_in;
  val = val_in;
  return buffer;
}


//
// MoqParameter
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqParameter& param){

  buffer.push(qtransport::to_uintV(param.param_type));
  buffer.push(qtransport::to_uintV(param.param_length));
  if (param.param_length) {
    buffer.push_lv(param.param_value);
  }
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqParameter &param) {

  if(!parse_uintV_field(buffer, param.param_type)) {
    return false;
  }

  if(!parse_uintV_field(buffer, param.param_length)) {
    return false;
  }

  if(param.param_length) {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    param.param_value = std::move(val.value());
  }

  return true;
}


MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &param) {
  buffer << param.param_type;
  buffer << param.param_length;
  if (param.param_length) {
    buffer << param.param_value;
  }

  return buffer;
}

MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &param) {
  buffer >> param.param_type;
  buffer >> param.param_length;
  if (static_cast<uint64_t>(param.param_length) > 0) {
    buffer >> param.param_value;
  }
  return buffer;
}


//
// Subscribe
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribe& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.track_alias));
  buffer.push_lv(msg.track_namespace);
  buffer.push_lv(msg.track_name);
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(msg.filter_type)));

  switch (msg.filter_type) {
    case FilterType::None:
    case FilterType::LatestGroup:
    case FilterType::LatestObject:
      break;
    case FilterType::AbsoluteStart: {
      buffer.push(qtransport::to_uintV(msg.start_group));
      buffer.push(qtransport::to_uintV(msg.start_object));
    }
      break;
    case FilterType::AbsoluteRange:
      buffer.push(qtransport::to_uintV(msg.start_group));
      buffer.push(qtransport::to_uintV(msg.start_object));
      buffer.push(qtransport::to_uintV(msg.end_group));
      buffer.push(qtransport::to_uintV(msg.end_object));
      break;
  }

  buffer.push(qtransport::to_uintV(msg.num_params));
  for (const auto& param: msg.track_params) {
    buffer.push(qtransport::to_uintV(static_cast<uint64_t>(param.param_type)));
    buffer.push(qtransport::to_uintV(param.param_length));
    buffer.push(param.param_value);
  }

  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribe &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!parse_bytes_field(buffer, msg.track_namespace)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 3: {
      if(!parse_bytes_field(buffer, msg.track_name)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 4: {
      const auto val = buffer.decode_uintV();
      if (!val) {
        return false;
      }
      auto filter = val.value();
      msg.filter_type = static_cast<FilterType>(filter);
      if (msg.filter_type == FilterType::LatestGroup
          || msg.filter_type == FilterType::LatestObject) {
        // we don't get further fields until parameters
        msg.current_pos = 9;
      } else {
        msg.current_pos += 1;
      }
    }
    break;
    case 5: {
      if (msg.filter_type == FilterType::AbsoluteStart
          || msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.start_group)) {
          return false;
        }
        msg.current_pos += 1;
      }
    }
    break;
    case 6: {
      if (msg.filter_type == FilterType::AbsoluteStart
          || msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.start_object)) {
          return false;
        }

        if (msg.filter_type == FilterType::AbsoluteStart) {
          msg.current_pos = 9;
        } else {
          msg.current_pos += 1;
        }
      }
    }
    break;
    case 7: {
      if (msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.end_group)) {
          return false;
        }
        msg.current_pos += 1;
      }
    }
    break;
    case 8: {
      if (msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.end_object)) {
          return false;
        }
        msg.current_pos += 1;
      }
    }
    break;
    case 9: {
      if (!msg.current_param.has_value()) {
        if (!parse_uintV_field(buffer, msg.num_params)) {
          return false;
        }
        msg.current_param = MoqParameter{};
      }
      // parse each param
      while (msg.num_params > 0) {
        if (!msg.current_param.value().param_type) {
          auto val = buffer.front();
          if (!val) {
            return false;
          }
          msg.current_param.value().param_type = *val;
          buffer.pop();
        }

        // decode param_len:<bytes>
        auto param = buffer.decode_bytes();
        if (!param) {
          return false;
        }
        msg.current_param.value().param_length = param->size();
        msg.current_param.value().param_value = param.value();
        msg.track_params.push_back(msg.current_param.value());
        msg.current_param = MoqParameter{};
        msg.num_params -= 1;
      }
      msg.parsing_completed = true;
    }
    break;
  }

  if (!msg.parsing_completed ) {
    return false;
  }

  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqUnsubscribe& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_UNSUBSCRIBE)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnsubscribe &msg) {

  if(!parse_uintV_field(buffer, msg.subscribe_id)) {
    return false;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeDone& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_DONE)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.status_code));
  buffer.push_lv(msg.reason_phrase);
  msg.content_exists ? buffer.push(static_cast<uint8_t>(1)) : buffer.push(static_cast<uint8_t>(0));
  if(msg.content_exists) {
    buffer.push(qtransport::to_uintV(msg.final_group_id));
    buffer.push(qtransport::to_uintV(msg.final_object_id));
  }

  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeDone &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.status_code)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      const auto val = buffer.decode_bytes();
      if (!val) {
        return false;
      }
      msg.reason_phrase = std::move(val.value());
      msg.current_pos += 1;
    }
    break;
    case 3: {
      const auto val = buffer.front();
      if (!val) {
        return false;
      }
      buffer.pop();
      msg.content_exists = (val.value()) == 1;
      msg.current_pos += 1;
      if (!msg.content_exists) {
        // nothing more to process.
        return true;
      }
    }
    break;
    case 4: {
      if(!parse_uintV_field(buffer, msg.final_group_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 5: {
      if(!parse_uintV_field(buffer, msg.final_object_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeOk& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_OK)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.expires));
  msg.content_exists ? buffer.push(static_cast<uint8_t>(1)) : buffer.push(static_cast<uint8_t>(0));
  if(msg.content_exists) {
    buffer.push(qtransport::to_uintV(msg.largest_group));
    buffer.push(qtransport::to_uintV(msg.largest_object));
  }
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeOk &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.expires)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      const auto val = buffer.front();
      if (!val) {
        return false;
      }
      buffer.pop();
      msg.content_exists = (val.value()) == 1;
      msg.current_pos += 1;
      if (!msg.content_exists) {
        // nothing more to process.
        return true;
      }
    }
    break;
    case 3: {
      if(!parse_uintV_field(buffer, msg.largest_group)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 4: {
      if(!parse_uintV_field(buffer, msg.largest_object)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeError& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.err_code));
  buffer.push_lv(msg.reason_phrase);
  buffer.push(qtransport::to_uintV(msg.track_alias));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeError &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.err_code)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      const auto val = buffer.decode_bytes();
      if (!val) {
        return false;
      }
      msg.reason_phrase = std::move(val.value());
      msg.current_pos += 1;
    }
    break;
    case 3: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}





//
// Announce
//


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounce& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE)));
  buffer.push_lv(msg.track_namespace);
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(0)));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer,
           MoqAnnounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.num_params) {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }
    msg.num_params = *val;
  }

  // parse each param
  while (msg.num_params > 0) {
    if (!msg.current_param.param_type) {
      auto val = buffer.front();
      if (!val) {
        return false;
      }
      msg.current_param.param_type = *val;
      buffer.pop();
    }

    // decode param_len:<bytes>
    auto param = buffer.decode_bytes();
    if (!param) {
      return false;
    }

    msg.current_param.param_length = param->size();
    msg.current_param.param_value = param.value();
    msg.params.push_back(msg.current_param);
    msg.current_param = {};
    msg.num_params -= 1;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceOk& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_OK)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceOk &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceError& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_ERROR)));
  buffer.push_lv(msg.track_namespace.value());
  buffer.push(qtransport::to_uintV(msg.err_code.value()));
  buffer.push_lv(msg.reason_phrase.value());
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceError &msg) {

  // read namespace
  if (!msg.track_namespace)
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.err_code) {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }

    msg.err_code = *val;
  }
  while (!msg.reason_phrase > 0) {
    auto reason = buffer.decode_bytes();
    if (!reason) {
      return false;
    }
    msg.reason_phrase = reason;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqUnannounce& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_UNANNOUNCE)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnannounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceCancel& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_CANCEL)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceCancel &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}

//
// Goaway
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqGoaway& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_GOAWAY)));
  buffer.push_lv(msg.new_session_uri);
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqGoaway &msg) {

  const auto val = buffer.decode_bytes();
  if (!val) {
    return false;
  }
  msg.new_session_uri = std::move(val.value());
  return true;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqGoaway &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_GOAWAY);
  buffer << msg.new_session_uri;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqGoaway &msg) {
  buffer >> msg.new_session_uri;
  return buffer;
}

//
// Object
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqObjectStream& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_OBJECT_STREAM)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.track_alias));
  buffer.push(qtransport::to_uintV(msg.group_id));
  buffer.push(qtransport::to_uintV(msg.object_id));
  buffer.push(qtransport::to_uintV(msg.priority));
  buffer.push_lv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectStream &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 3: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 4: {
      if(!parse_uintV_field(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 5: {
      const auto val = buffer.decode_bytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqObjectDatagram& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_OBJECT_DATAGRAM)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.track_alias));
  buffer.push(qtransport::to_uintV(msg.group_id));
  buffer.push(qtransport::to_uintV(msg.object_id));
  buffer.push(qtransport::to_uintV(msg.priority));
  buffer.push_lv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectDatagram &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 3: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 4: {
      if(!parse_uintV_field(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 5: {
      const auto val = buffer.decode_bytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}



qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamHeaderTrack& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_STREAM_HEADER_TRACK)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.track_alias));
  buffer.push(qtransport::to_uintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderTrack &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!parse_uintV_field(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamTrackObject& msg){

  buffer.push(qtransport::to_uintV(msg.group_id));
  buffer.push(qtransport::to_uintV(msg.object_id));
  buffer.push_lv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamTrackObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      const auto val = buffer.decode_bytes();
      if(!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }
  return true;
}



qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamHeaderGroup& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_STREAM_HEADER_GROUP)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id));
  buffer.push(qtransport::to_uintV(msg.track_alias));
  buffer.push(qtransport::to_uintV(msg.group_id));
  buffer.push(qtransport::to_uintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderGroup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 3: {
      if(!parse_uintV_field(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamGroupObject& msg){

  buffer.push(qtransport::to_uintV(msg.object_id));
  buffer.push_lv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamGroupObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
    }
    break;
    case 1: {
      const auto val = buffer.decode_bytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
    }
    break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}

// Client Setup message
qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqClientSetup& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_CLIENT_SETUP)));
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(msg.supported_versions.size())));
  // versions
  for (const auto& ver: msg.supported_versions) {
    buffer.push(qtransport::to_uintV(ver));
  }

  /// num params
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(1)));
  // role param
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(msg.role_parameter.param_type)));
  buffer.push_lv(msg.role_parameter.param_value);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqClientSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.num_versions)) {
        return false;
      }
      msg.current_pos += 1;
    }
    case 1: {
      while (msg.num_versions > 0) {
        uint64_t version{ 0 };
        if (!parse_uintV_field(buffer, version)) {
          return false;
        }
        msg.supported_versions.push_back(version);
        msg.num_versions -= 1;
      }
      msg.current_pos += 1;
    }
    break;
    case 2: {
      if(!msg.num_params.has_value()) {
        auto params = uint64_t {0};
        if (!parse_uintV_field(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          auto val = buffer.front();
          if (!val) {
            return false;
          }
          msg.current_param = MoqParameter{};
          msg.current_param->param_type = *val;
          buffer.pop();
        }

        auto param = buffer.decode_bytes();
        if (!param) {
          return false;
        }
        msg.current_param->param_length = param->size();
        msg.current_param->param_value = param.value();
        static_cast<ParameterType>(msg.current_param->param_type) == ParameterType::Role  ?
          msg.role_parameter = std::move(msg.current_param.value()) : msg.path_parameter = std::move(msg.current_param.value());
        msg.current_param = MoqParameter{};
        msg.num_params.value() -= 1;
      }

      msg.parse_completed = true;
    }
    break;
  }

  if (!msg.parse_completed) {
    return false;
  }

  return true;
}



// Server Setup message

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqServerSetup& msg){

  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SERVER_SETUP)));
  buffer.push(qtransport::to_uintV(msg.selection_version));

  /// num params
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(1)));
  // role param
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(msg.role_parameter.param_type)));
  buffer.push_lv(msg.role_parameter.param_value);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqServerSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.selection_version)) {
        return false;
      }
      msg.current_pos += 1;
    }
    case 1: {
      if(!msg.num_params.has_value()) {
        auto params = uint64_t {0};
        if (!parse_uintV_field(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          auto val = buffer.front();
          if (!val) {
            return false;
          }
          msg.current_param = MoqParameter{};
          msg.current_param->param_type = *val;
          buffer.pop();
        }

        auto param = buffer.decode_bytes();
        if (!param) {
          return false;
        }
        msg.current_param->param_length = param->size();
        msg.current_param->param_value = param.value();
        static_cast<ParameterType>(msg.current_param->param_type) == ParameterType::Role  ?
                                                                                         msg.role_parameter = std::move(msg.current_param.value()) : msg.path_parameter = std::move(msg.current_param.value());
        msg.current_param = MoqParameter{};
        msg.num_params.value() -= 1;
      }
      msg.parse_completed = true;
    }
    break;
  }

  if (!msg.parse_completed) {
    return false;
  }

  return true;
}

}