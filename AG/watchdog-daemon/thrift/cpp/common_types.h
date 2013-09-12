/**
 * Autogenerated by Thrift Compiler (0.8.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#ifndef common_TYPES_H
#define common_TYPES_H

#include <Thrift.h>
#include <TApplicationException.h>
#include <protocol/TProtocol.h>
#include <transport/TTransport.h>



namespace watchdog {

typedef struct _AGDaemonID__isset {
  _AGDaemonID__isset() : addr(false), port(false), frequency(false), ag_map(false) {}
  bool addr;
  bool port;
  bool frequency;
  bool ag_map;
} _AGDaemonID__isset;

class AGDaemonID {
 public:

  static const char* ascii_fingerprint; // = "2B119B0EE8560DB9D262867AAFEB89DB";
  static const uint8_t binary_fingerprint[16]; // = {0x2B,0x11,0x9B,0x0E,0xE8,0x56,0x0D,0xB9,0xD2,0x62,0x86,0x7A,0xAF,0xEB,0x89,0xDB};

  AGDaemonID() : addr(""), port(0), frequency(0) {
  }

  virtual ~AGDaemonID() throw() {}

  std::string addr;
  int32_t port;
  int16_t frequency;
  std::map<int32_t, std::string>  ag_map;

  _AGDaemonID__isset __isset;

  void __set_addr(const std::string& val) {
    addr = val;
  }

  void __set_port(const int32_t val) {
    port = val;
  }

  void __set_frequency(const int16_t val) {
    frequency = val;
  }

  void __set_ag_map(const std::map<int32_t, std::string> & val) {
    ag_map = val;
  }

  bool operator == (const AGDaemonID & rhs) const
  {
    if (!(addr == rhs.addr))
      return false;
    if (!(port == rhs.port))
      return false;
    if (!(frequency == rhs.frequency))
      return false;
    if (!(ag_map == rhs.ag_map))
      return false;
    return true;
  }
  bool operator != (const AGDaemonID &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const AGDaemonID & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};

typedef struct _PingResponse__isset {
  _PingResponse__isset() : live_set(false), dead_set(false), id(false) {}
  bool live_set;
  bool dead_set;
  bool id;
} _PingResponse__isset;

class PingResponse {
 public:

  static const char* ascii_fingerprint; // = "225D317ACC2239665D64C9293D3A64F1";
  static const uint8_t binary_fingerprint[16]; // = {0x22,0x5D,0x31,0x7A,0xCC,0x22,0x39,0x66,0x5D,0x64,0xC9,0x29,0x3D,0x3A,0x64,0xF1};

  PingResponse() : id(0) {
  }

  virtual ~PingResponse() throw() {}

  std::set<int32_t>  live_set;
  std::set<int32_t>  dead_set;
  int32_t id;

  _PingResponse__isset __isset;

  void __set_live_set(const std::set<int32_t> & val) {
    live_set = val;
  }

  void __set_dead_set(const std::set<int32_t> & val) {
    dead_set = val;
  }

  void __set_id(const int32_t val) {
    id = val;
  }

  bool operator == (const PingResponse & rhs) const
  {
    if (!(live_set == rhs.live_set))
      return false;
    if (!(dead_set == rhs.dead_set))
      return false;
    if (!(id == rhs.id))
      return false;
    return true;
  }
  bool operator != (const PingResponse &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const PingResponse & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

};

} // namespace

#endif
