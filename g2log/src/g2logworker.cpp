/** ==========================================================================
 * 2011 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
 * with no warranties. This code is yours to share, use and modify with no
 * strings attached and no restrictions or obligations.
 * ============================================================================
 * Filename:g2LogWorker.cpp  Framework for Logging and Design By Contract
 * Created: 2011 by Kjell Hedström
 *
 * PUBLIC DOMAIN and Not under copywrite protection. First published at KjellKod.cc
 * ********************************************* */

#include "g2logworker.hpp"

#include <cassert>
#include <functional>
#include "active.hpp"
#include "g2log.hpp"
#include "g2time.hpp"
#include "g2future.hpp"
#include "crashhandler.hpp"

#include <iostream> // remove

namespace g2 {
   
struct LogWorkerImpl {
   typedef std::shared_ptr<g2::internal::SinkWrapper> SinkWrapperPtr;
   std::unique_ptr<kjellkod::Active> _bg;
   std::vector<SinkWrapperPtr> _sinks;

   LogWorkerImpl() : _bg(kjellkod::Active::createActive()) {}

   ~LogWorkerImpl() {
      _bg.reset();
   }

   void bgSave(g2::LogMessagePtr msgPtr) {     
      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));
      
      for (auto& sink : _sinks) { 
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }

      if (_sinks.empty()) {
         std::string err_msg{"g2logworker has no sinks. Message: ["};
         err_msg.append(uniqueMsg.get()->toString()).append({"]\n"});
         std::cerr << err_msg;
      }
   }

   void bgFatal(FatalMessagePtr msgPtr) {
      std::string signal = msgPtr.get()->signal();
      auto fatal_signal_id = msgPtr.get()->_signal_id;

      std::unique_ptr<LogMessage> uniqueMsg(std::move(msgPtr.get()));
      uniqueMsg->write().append("\nExiting after fatal event  (").append(uniqueMsg->level());
      uniqueMsg->write().append("). Exiting with signal: ").append(signal)
              .append("\nLog content flushed flushed sucessfully to sink\n\n");
      
      std::cerr << uniqueMsg->message() << std::flush;
      for (auto& sink : _sinks) {
         LogMessage msg(*(uniqueMsg));
         sink->send(LogMessageMover(std::move(msg)));
      }
      // only the active logger can receive a FATAL call, so it's safe to shut down logging now
       g2::internal::shutDownLogging(); 
       _sinks.clear(); // flush all queues


      internal::exitWithDefaultSignalHandler(fatal_signal_id);
      // should never reach this point
      perror("g2log exited after receiving FATAL trigger. Flush message status: ");
   }
};




// Default constructor will have one sink: g2filesink.
LogWorker::LogWorker()
: _pimpl(std2::make_unique<LogWorkerImpl>()) {
}

LogWorker::~LogWorker() {
   g2::internal::shutDownLoggingForActiveOnly(this);
   auto bg_clear_sink_call = [this] { _pimpl->_sinks.clear(); };
   auto token_cleared = g2::spawn_task(bg_clear_sink_call, _pimpl->_bg.get());
   token_cleared.wait();
}

void LogWorker::save(LogMessagePtr msg) {
   _pimpl->_bg->send([this, msg] {_pimpl->bgSave(msg); }); 
}

void LogWorker::fatal(FatalMessagePtr fatal_message) {
   _pimpl->_bg->send([this, fatal_message] { _pimpl->bgFatal(fatal_message); });
}

void LogWorker::addWrappedSink(std::shared_ptr<g2::internal::SinkWrapper> sink) {
   auto bg_addsink_call = [this, sink] { _pimpl->_sinks.push_back(sink); };
   auto token_done = g2::spawn_task(bg_addsink_call, _pimpl->_bg.get());
   token_done.wait();
}


// Gör en egen super simpel klass/struct med 
// DefaultFilLogger den ska INTE vara i g2logworker.cpp

g2::DefaultFileLogger LogWorker::createWithDefaultLogger(const std::string& log_prefix, const std::string& log_directory) {
   return g2::DefaultFileLogger(log_prefix, log_directory);
}

std::unique_ptr<LogWorker> LogWorker::createWithNoSink() {
   return std::unique_ptr<LogWorker>(new LogWorker);
}

DefaultFileLogger::DefaultFileLogger(const std::string& log_prefix, const std::string& log_directory)
: worker(LogWorker::createWithNoSink())
, sink(worker->addSink(std2::make_unique<g2::FileSink>(log_prefix, log_directory), &FileSink::fileWrite)) {
}

} // g2
