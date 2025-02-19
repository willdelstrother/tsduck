//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2023, Thierry Lelegard
// BSD-2-Clause license, see LICENSE.txt file or https://tsduck.io/license
//
//----------------------------------------------------------------------------
//
//  Manually compute CRC-32 values as done in MPEG sections.
//
//----------------------------------------------------------------------------

#include "tsMain.h"
#include "tsArgs.h"
#include "tsCRC32.h"
#include "tsSysInfo.h"
#include "tsSysUtils.h"
TS_MAIN(MainCode);


//----------------------------------------------------------------------------
//  Command line options
//----------------------------------------------------------------------------

namespace {
    class Options: public ts::Args
    {
        TS_NOBUILD_NOCOPY(Options);
    public:
        Options(int argc, char *argv[]);

        ts::UStringVector infiles {};          // Input file names.
        ts::ByteBlock     indata {};           // Raw input data.
        bool              show_name = false;   // Show file name on input.
        bool              accelerated = false; // Check if the computation of CRC32 is accelerated.
    };
}

Options::Options(int argc, char *argv[]) :
    Args(u"Compute MPEG-style CRC32 values", u"[options] [filename ...]")
{
    option(u"", 0, FILENAME, 0, UNLIMITED_COUNT);
    help(u"", u"Any number of binary input files (standard input if omitted).");

    option(u"accelerated", 'a');
    help(u"accelerated", u"Check if the computation of CRC32 is accelerated using specialized instructions.");

    option(u"data", 'd', HEXADATA);
    help(u"data", u"Raw input data instead of input files. Use hexadecimal digits.");

    analyze(argc, argv);

    getValues(infiles);
    getHexaValue(indata, u"data");
    show_name = verbose() || infiles.size() + !indata.empty() > 1;
    accelerated = present(u"accelerated");

    exitOnError();
}


//----------------------------------------------------------------------------
// Perform the CRC32 computation on one input file.
//----------------------------------------------------------------------------

namespace {
    void ProcessFile(Options& opt, const ts::UString& filename)
    {
        ts::CRC32 crc;
        ts::UString prefix;
        std::istream* in = nullptr;
        std::ifstream file;

        // Open input file (standard input if no file is specified or file name is empty).
        if (filename.empty() || filename == u"-") {
            // Use standard input.
            in = &std::cin;
            // Try to put standard input in binary mode
            ts::SetBinaryModeStdin(opt);
            if (opt.show_name) {
                prefix = u"standard input: ";
            }
        }
        else {
            // Dump named files. Open the file in binary mode. Will be closed by destructor.
            in = &file;
            file.open(filename.toUTF8().c_str(), std::ios::binary);
            if (!file) {
                opt.error(u"cannot open file %s", {filename});
                return;
            }
            if (opt.show_name) {
                prefix = filename + u": ";
            }
        }

        // Read file, compute CRC.
        std::vector<char> buffer(1024 * 2024);
        while (!in->eof()) {
            in->read(&buffer[0], buffer.size());
            size_t insize = size_t(in->gcount());
            crc.add(buffer.data(), insize);
        }
        std::cout << ts::UString::Format(u"%s%08X", {prefix, crc.value()}) << std::endl;
    }
}


//----------------------------------------------------------------------------
//  Program entry point
//----------------------------------------------------------------------------

int MainCode(int argc, char *argv[])
{
    // Decode command line.
    Options opt(argc, argv);

    // Check the presence of CRC32 acceleration.
    if (opt.accelerated) {
        const bool yes = ts::SysInfo::Instance().crcInstructions();
        if (opt.verbose()) {
            std::cout << "CRC32 computation is " << (yes ? "" : "not ") << "accelerated" << std::endl;
        }
        else {
            std::cout << ts::UString::YesNo(yes) << std::endl;
        }
    }

    // Process explicit input data.
    if (!opt.indata.empty()) {
        const ts::CRC32 crc(opt.indata.data(), opt.indata.size());
        std::cout << ts::UString::Format(u"%s%08X", {opt.show_name ? u"input data: " : u"", crc.value()}) << std::endl;
    }

    // Process input files.
    if (opt.infiles.empty() && opt.indata.empty() && !opt.accelerated) {
        // Process standard input.
        ProcessFile(opt, ts::UString());
    }
    else {
        // Process named files.
        for (const auto& name : opt.infiles) {
            ProcessFile(opt, name);
        }
    }

    return opt.valid() ? EXIT_SUCCESS : EXIT_FAILURE;
}
