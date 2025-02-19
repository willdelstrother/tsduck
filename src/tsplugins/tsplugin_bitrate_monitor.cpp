//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Jerome Leveque, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  Transport stream processor shared library:
//  Monitor PID or TS bitrate
//
//----------------------------------------------------------------------------

#include "tsPluginRepository.h"
#include "tsForkPipe.h"
#include "tsTime.h"
#include "tsMonotonic.h"
#include "tsSingleDataStatistics.h"


//----------------------------------------------------------------------------
// Plugin definition
//----------------------------------------------------------------------------

namespace ts {
    class BitrateMonitorPlugin: public ProcessorPlugin
    {
        TS_PLUGIN_CONSTRUCTORS(BitrateMonitorPlugin);
    public:
        // Implementation of plugin API
        virtual bool getOptions() override;
        virtual bool start() override;
        virtual bool stop() override;
        virtual Status processPacket(TSPacket&, TSPacketMetadata&) override;
        virtual bool handlePacketTimeout() override;

    private:
        // Default values
        static constexpr BitRate::int_t DEFAULT_BITRATE_MIN = 10;
        static constexpr BitRate::int_t DEFAULT_BITRATE_MAX = 0xFFFFFFFF;
        static constexpr size_t DEFAULT_TIME_WINDOW_SIZE = 5;

        // Type indicating status of current bitrate, regarding allowed range.
        enum RangeStatus {LOWER, IN_RANGE, GREATER};

        // Description of what is received during approximately one second.
        class Period
        {
        public:
            NanoSecond    duration = 0;  // Actual duration in nanoseconds.
            PacketCounter packets = 0;   // Total number of packets.
            PacketCounter non_null = 0;  // Total number of non-null packets.

            // Constructor.
            Period() = default;

            // Clear content.
            void clear() { duration = 0; packets = non_null = 0; }
        };

        // Command line options.
        bool             _full_ts = false;       // Monitor full TS.
        bool             _summary = false;       // Display a final summary.
        PID              _first_pid = PID_NULL;  // First monitored PID (for messages).
        size_t           _pid_count = 0;         // Number of PID's to monitor.
        PIDSet           _pids {};               // Monitored PID's.
        UString          _tag {};                // Message tag.
        BitRate          _min_bitrate = 0;       // Minimum allowed bitrate.
        BitRate          _max_bitrate = 0;       // Maximum allowed bitrate.
        Second           _periodic_bitrate = 0;  // Report bitrate at regular intervals, even if in range.
        Second           _periodic_command = 0;  // Run alarm command at regular intervals, even if in range.
        size_t           _window_size = 0;       // Size (in seconds) of the time window, used to compute bitrate.
        UString          _alarm_command {};      // Alarm command name.
        UString          _alarm_prefix {};       // Prefix for alarm messages.
        UString          _alarm_target {};       // "target" parameter to the alarm command.
        TSPacketLabelSet _labels_below {};       // Set these labels on all packets when bitrate is below normal.
        TSPacketLabelSet _labels_normal {};      // Set these labels on all packets when bitrate is normal.
        TSPacketLabelSet _labels_above {};       // Set these labels on all packets when bitrate is above normal.
        TSPacketLabelSet _labels_go_below {};    // Set these labels on one packet when bitrate goes below normal.
        TSPacketLabelSet _labels_go_normal {};   // Set these labels on one packet when bitrate goes back to normal.
        TSPacketLabelSet _labels_go_above {};    // Set these labels on one packet when bitrate goes above normal.

        // Working data.
        Second              _bitrate_countdown = 0;   // Countdown to report bitrate.
        Second              _command_countdown = 0;   // Countdown to run alarm command.
        RangeStatus         _last_bitrate_status = LOWER; // Status of the last bitrate, regarding allowed range.
        Monotonic           _last_second {};          // System time at last measurement point.
        bool                _startup = false;         // Measurement in progress.
        size_t              _periods_index = 0;       // Index for packet number array.
        std::vector<Period> _periods {};              // Number of packets received during last time window, second per second.
        TSPacketLabelSet    _labels_next {};          // Set these labels on next packet.
        SingleDataStatistics<int64_t> _stats {};      // Bitrate statistics.
        SingleDataStatistics<int64_t> _net_stats {};  // Non-null bitrate statistics.

        // Compute bitrate. Report any alarm.
        void computeBitrate();

        // Check time and compute bitrate when necessary.
        void checkTime();
    };
}

TS_REGISTER_PROCESSOR_PLUGIN(u"bitrate_monitor", ts::BitrateMonitorPlugin);


//----------------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------------

ts::BitrateMonitorPlugin::BitrateMonitorPlugin(TSP* tsp_) :
    ProcessorPlugin(tsp_, u"Monitor bitrate for TS or a given set of PID's", u"[options]")
{
    // The PID was previously passed as argument. We now use option --pid.
    // We still accept the argument for legacy, but not both.
    option(u"", 0, PIDVAL, 0, UNLIMITED_COUNT);
    option(u"pid", 0, PIDVAL, 0, UNLIMITED_COUNT);
    help(u"pid", u"pid1[-pid2]",
         u"Specifies the PID or set of PID's to monitor. "
         u"By default, when no --pid is specified, monitor the bitrate of the full TS. "
         u"Several --pid options may be specified. "
         u"When several PID's are specified, the tested bitrate is the global bitrate of all the selected PID's.");

    option(u"alarm-command", 'a', STRING);
    help(u"alarm-command", u"'command'",
         u"Command to run when the bitrate goes either out of range or back to normal. "
         u"The command receives the following additional parameters:\n\n"
         u"1. A human-readable alarm message.\n"
         u"2. Either \"ts\" or the decimal integer value of the first PID to monitor.\n"
         u"3. Bitrate alarm state, one of \"lower\", \"greater\", \"normal\".\n"
         u"4. Current bitrate in b/s (decimal integer).\n"
         u"5. Minimum bitrate in b/s (decimal integer).\n"
         u"6. Maximum bitrate in b/s (decimal integer).\n"
         u"7. Net bitrate, without null packets, in b/s (decimal integer).");

    option(u"time-interval", 't', UINT16);
    help(u"time-interval",
         u"Time interval (in seconds) used to compute the bitrate. "
         u"Default: " + UString::Decimal(DEFAULT_TIME_WINDOW_SIZE) + u" s.");

    option<BitRate>(u"min");
    help(u"min",
         u"Set minimum allowed value for bitrate (bits/s). "
         u"Default: " + UString::Decimal(DEFAULT_BITRATE_MIN) + u" b/s.");

    option<BitRate>(u"max");
    help(u"max",
         u"Set maximum allowed value for bitrate (bits/s). "
         u"Default: " + UString::Decimal(DEFAULT_BITRATE_MAX) + u" b/s.");

    option(u"periodic-bitrate", 'p', POSITIVE);
    help(u"periodic-bitrate",
         u"Always report bitrate at the specific intervals in seconds, even if the bitrate is in range.");

    option(u"periodic-command", 0, POSITIVE);
    help(u"periodic-command",
         u"Run the --alarm-command at the specific intervals in seconds, even if the bitrate is in range. "
         u"With this option, the alarm command is run on state change and at periodic intervals.");

    option(u"set-label-below", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-below", u"label1[-label2]",
         u"Set the specified labels on all packets while the bitrate is below normal. "
         u"Several --set-label-below options may be specified.");

    option(u"set-label-go-below", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-go-below", u"label1[-label2]",
         u"Set the specified labels on one packet when the bitrate goes below normal. "
         u"Several --set-label-go-below options may be specified.");

    option(u"set-label-above", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-above", u"label1[-label2]",
         u"Set the specified labels on all packets while the bitrate is above normal. "
         u"Several --set-label-above options may be specified.");

    option(u"set-label-go-above", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-go-above", u"label1[-label2]",
         u"Set the specified labels on one packet when the bitrate goes above normal. "
         u"Several --set-label-go-above options may be specified.");

    option(u"set-label-normal", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-normal", u"label1[-label2]",
         u"Set the specified labels on all packets while the bitrate is normal (within range). "
         u"Several --set-label-normal options may be specified.");

    option(u"set-label-go-normal", 0, INTEGER, 0, UNLIMITED_COUNT, 0, TSPacketLabelSet::MAX);
    help(u"set-label-go-normal", u"label1[-label2]",
         u"Set the specified labels on one packet when the bitrate goes back to normal (within range). "
         u"Several --set-label-go-normal options may be specified.");

    option(u"summary", 's');
    help(u"summary",
         u"Display a final summary of bitrate statistics.");

    option(u"tag", 0, STRING);
    help(u"tag", u"'string'",
         u"Message tag to be displayed in alarms. "
         u"Useful when the plugin is used several times in the same process.");
}


//----------------------------------------------------------------------------
// Get options method
//----------------------------------------------------------------------------

bool ts::BitrateMonitorPlugin::getOptions()
{
    bool ok = true;

    // Get the PID. Accept either --pid or legacy argument, but not both.
    const bool got_legacy_arg = present(u"");
    const bool got_pid_option = present(u"pid");
    const UChar* const pid_opt_name = got_legacy_arg ? u"" : u"pid";

    _full_ts = !got_legacy_arg && !got_pid_option;
    _summary = present(u"summary");
    _pid_count = _full_ts ? PID_MAX : count(pid_opt_name);
    getIntValue(_first_pid, pid_opt_name, PID_NULL);
    getIntValues(_pids, pid_opt_name, true);

    if (got_legacy_arg && got_pid_option) {
        tsp->error(u"specify either --pid or legacy argument, but not both");
        ok = false;
    }

    // Get options
    getValue(_tag, u"tag");
    getValue(_alarm_command, u"alarm-command");
    getIntValue(_window_size, u"time-interval", DEFAULT_TIME_WINDOW_SIZE);
    getValue(_min_bitrate, u"min", DEFAULT_BITRATE_MIN);
    getValue(_max_bitrate, u"max", DEFAULT_BITRATE_MAX);
    getIntValue(_periodic_bitrate, u"periodic-bitrate", 0);
    getIntValue(_periodic_command, u"periodic-command", 0);
    getIntValues(_labels_below, u"set-label-below");
    getIntValues(_labels_normal, u"set-label-normal");
    getIntValues(_labels_above, u"set-label-above");
    getIntValues(_labels_go_below, u"set-label-go-below");
    getIntValues(_labels_go_normal, u"set-label-go-normal");
    getIntValues(_labels_go_above, u"set-label-go-above");

    if (_min_bitrate > _max_bitrate) {
        tsp->error(u"bad parameters, bitrate min (%'d) > max (%'d), exiting", {_min_bitrate, _max_bitrate});
        ok = false;
    }
    if (_periodic_command > 0 && _alarm_command.empty()) {
        tsp->warning(u"no --alarm-command set, --periodic-command ignored");
        _periodic_command = 0;
    }

    // Prefix for alarm messages.
    _alarm_prefix = _tag;
    _alarm_target.clear();
    if (!_alarm_prefix.empty()) {
        _alarm_prefix.append(u": ");
    }
    if (_full_ts) {
        _alarm_prefix.append(u"TS");
        _alarm_target = u"ts";
    }
    else {
        _alarm_prefix.format(u"PID 0x%X (%<d)", {_first_pid});
        _alarm_target.format(u"%d", {_first_pid});
    }

    return ok;
}


//----------------------------------------------------------------------------
// Start method
//----------------------------------------------------------------------------

bool ts::BitrateMonitorPlugin::start()
{
    // Try to get 2 milliseconds as timer precision (if possible).
    Monotonic::SetPrecision(2 * NanoSecPerMilliSec);

    // Initialize array packets count.
    _periods.resize(_window_size);
    for (auto& p : _periods) {
        p.clear();
    }

    _periods_index = 0;
    _labels_next.reset();
    _bitrate_countdown = _periodic_bitrate;
    _command_countdown = _periodic_command;
    _last_bitrate_status = IN_RANGE;
    _last_second.getSystemTime();
    _startup = true;
    _stats.reset();
    _net_stats.reset();

    // We must never wait for packets more than one second.
    tsp->setPacketTimeout(MilliSecPerSec);

    return true;
}


//----------------------------------------------------------------------------
// Stop method
//----------------------------------------------------------------------------

bool ts::BitrateMonitorPlugin::stop()
{
    if (_summary) {
        if (_full_ts) {
            tsp->info(u"%s average bitrate: %'d bits/s, average net bitrate: %'d bits/s", {_alarm_prefix, _stats.meanRound(), _net_stats.meanRound()});
        }
        else {
            tsp->info(u"%s average bitrate: %'d bits/s", {_alarm_prefix, _stats.meanRound()});
        }
    }
    return true;
}


//----------------------------------------------------------------------------
// Compute bitrate, report alarms.
//----------------------------------------------------------------------------

void ts::BitrateMonitorPlugin::computeBitrate()
{
    // Compute total duration and packets.
    NanoSecond duration = 0;
    PacketCounter total_pkt_count = 0;
    PacketCounter non_null_count = 0;
    for (auto& p : _periods) {
        duration += p.duration;
        total_pkt_count += p.packets;
        non_null_count += p.non_null;
    }

    // Nanoseconds is an unusually large precision which may lead to overflows.
    // Using seconds is not precise enough. Use microseconds.
    duration /= NanoSecPerMicroSec;
    BitRate bitrate(0);
    BitRate net_bitrate(0);
    if (duration > 0) {
        bitrate = (BitRate(total_pkt_count) * PKT_SIZE_BITS * MicroSecPerSec) / duration;
        net_bitrate = (BitRate(non_null_count) * PKT_SIZE_BITS * MicroSecPerSec) / duration;
    }

    // Accumulate statistics for the final report.
    if (_summary) {
        _stats.feed(bitrate.toInt64());
        _net_stats.feed(net_bitrate.toInt64());
    }

    // Check the bitrate value, regarding the allowed range.
    RangeStatus new_bitrate_status = IN_RANGE;
    const UChar* alarm_status = nullptr;
    if (bitrate < _min_bitrate) {
        new_bitrate_status = LOWER;
        alarm_status = u"lower";
    }
    else if (bitrate > _max_bitrate) {
        new_bitrate_status = GREATER;
        alarm_status = u"greater";
    }
    else {
        new_bitrate_status = IN_RANGE;
        alarm_status = u"normal";
    }

    // Periodic bitrate display.
    if (_periodic_bitrate > 0 && --_bitrate_countdown <= 0) {
        _bitrate_countdown = _periodic_bitrate;
        if (_full_ts) {
            tsp->info(u"%s, %s bitrate: %'d bits/s, net bitrate: %'d bits/s", {Time::CurrentLocalTime().format(Time::DATETIME), _alarm_prefix, bitrate, net_bitrate});
        }
        else {
            tsp->info(u"%s, %s bitrate: %'d bits/s", {Time::CurrentLocalTime().format(Time::DATETIME), _alarm_prefix, bitrate});
        }
    }

    // Periodic command launch.
    bool run_command = false;
    if (_periodic_command > 0 && --_command_countdown <= 0) {
        _command_countdown = _periodic_command;
        run_command = true;
    }

    // Check if the bitrate status has changed.
    const bool state_change = new_bitrate_status != _last_bitrate_status;

    if (state_change || run_command) {

        // Format an alarm message.
        UString alarm_message;
        alarm_message.format(u"%s bitrate (%'d bits/s)", {_alarm_prefix, bitrate});
        if (state_change) {
            switch (new_bitrate_status) {
                case LOWER:
                    alarm_message.format(u" is lower than allowed minimum (%'d bits/s)", {_min_bitrate});
                    _labels_next |= _labels_go_below;
                    break;
                case IN_RANGE:
                    alarm_message.format(u" is back in allowed range (%'d-%'d bits/s)", {_min_bitrate, _max_bitrate});
                    _labels_next |= _labels_go_normal;
                    break;
                case GREATER:
                    alarm_message.format(u" is greater than allowed maximum (%'d bits/s)", {_max_bitrate});
                    _labels_next |= _labels_go_above;
                    break;
                default:
                    assert(false); // should not get there
            }

            // Report alarm message as a tsp warning in case of state change.
            tsp->warning(alarm_message);
        }

        // Call alarm script if defined.
        // The command is run asynchronously, do not wait for completion.
        if (!_alarm_command.empty()) {
            UString command;
            command.format(u"%s \"%s\" %s %s %d %d %d %d", {_alarm_command, alarm_message, _alarm_target, alarm_status, bitrate, _min_bitrate, _max_bitrate, net_bitrate});
            ForkPipe::Launch(command, *tsp, ForkPipe::STDERR_ONLY, ForkPipe::STDIN_NONE);
        }

        // Update status
        _last_bitrate_status = new_bitrate_status;
    }
}


//----------------------------------------------------------------------------
// Check time and compute bitrate when necessary.
//----------------------------------------------------------------------------

void ts::BitrateMonitorPlugin::checkTime()
{
    // Current system time.
    Monotonic now(true);
    const NanoSecond since_last_second = now - _last_second;

    // New second : compute the bitrate for the last time window
    if (since_last_second >= NanoSecPerSec) {

        // Exact duration of the last period and restart a new period.
        _periods[_periods_index].duration = since_last_second;
        _last_second = now;

        // Bitrate computation is done only when the packet counter
        // array if fully filled (to avoid bad values at startup).
        if (!_startup) {
            computeBitrate();
        }

        // update index, and reset packet count.
        _periods_index = (_periods_index + 1) % _periods.size();
        _periods[_periods_index].clear();

        // We are no more at startup if the index cycles.
        if (_startup) {
            _startup = _periods_index != 0;
        }
    }
}


//----------------------------------------------------------------------------
// Packet timeout processing method.
//----------------------------------------------------------------------------

bool ts::BitrateMonitorPlugin::handlePacketTimeout()
{
    // Check time and bitrates.
    checkTime();

    // Always continue waiting, never abort.
    return true;
}


//----------------------------------------------------------------------------
// Packet processing method.
//----------------------------------------------------------------------------

ts::ProcessorPlugin::Status ts::BitrateMonitorPlugin::processPacket(TSPacket& pkt, TSPacketMetadata& pkt_data)
{
    // If packet's PID matches, increment the number of packets received during the current second.
    if (_pids.test(pkt.getPID())) {
        _periods[_periods_index].packets++;
        if (pkt.getPID() != PID_NULL) {
            _periods[_periods_index].non_null++;
        }
    }

    // Check time and bitrates.
    checkTime();

    // Set labels according to trigger.
    pkt_data.setLabels(_labels_next);
    _labels_next.reset();

    // Set labels according to state.
    switch (_last_bitrate_status) {
        case LOWER:
            pkt_data.setLabels(_labels_below);
            break;
        case IN_RANGE:
            pkt_data.setLabels(_labels_normal);
            break;
        case GREATER:
            pkt_data.setLabels(_labels_above);
            break;
        default:
            assert(false); // should not get there
    }

    // Pass all packets
    return TSP_OK;
}
