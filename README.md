# rotation_logger

The Rotation Logger program provides a simple rotating logger.
It is similar to tee, i.e. it copies from standard input to standard output, and also to a log file.
Unlike tee, the file size and/or age is limited, and when the size or age exceeds the specified
threshold, a new file is created and output is directed to the new file.

### Usage

    usage: rotation_logger [OPTIONS] directory prefix
           rotation_logger  --help|-h
           rotation_logger  --version|-v

### Parameters

directory     the location (relative of absolute) where the log files are to be created.
              If the specified directory does not exists, it is created.
              rotation_logger essentially does: mkdir -p '<directory>'  on start-up.

prefix        this specifies the file name prefix given to the log files. The suffix is
              always ".log". The full file filename is <prefix>_YYYY-MM-DD_HH-MM-SS.log

### Options

--age,-a      age limit allowed for each file, expressed in seconds. It may be qualified
              with m, h, d or w for minutes, hours, days and weeks respectively.
              The default is 1d. The value is forced to be >= 10s.

--size,-s     size limit allowed for each file, expressed in bytes. It may be qualified
              with K, M or G for kilo, mega and giga bytes respectively. The default is 50M.
              The value is forced to be >= 20.

--keep,k      number of files to keep. This is above and beyond the current file.
              The default is 40. The value is forced to be >= 1.

--help|-h     show this help information and exit.

--version|-v  show program version and exit.
