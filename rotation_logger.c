/* rotation_logger.c
 * 
 * Copyright (C) 2019-2020  Andrew C. Starritt
 *
 * The rotation logger is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License.
 *
 * You can also redistribute rotation logger and/or modify it under the
 * terms of the Lesser GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version when this library is disributed with and as part of the
 * EPICS QT Framework (https://github.com/qtepics).
 *
 * The rotation logger is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License and
 * the Lesser GNU General Public License along with rotation logger.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact details:
 * andrew.starritt@gmail.com
 * PO Box 3118, Prahran East, Victoria 3181, Australia.
 *
 */

/* rotation_logger 
 * 
 * 
 */

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FULL_PATH_LEN    240
#define VERSION          "1.1.5"

//------------------------------------------------------------------------------
//
static void perrorf (const char* format, ...)
{
   char message [240];
   va_list arguments;
   va_start (arguments, format);
   vsnprintf (message, sizeof (message), format, arguments);
   va_end (arguments);
   perror (message);
}

//------------------------------------------------------------------------------
//
static void printUsage()
{
   printf ("usage: rotation_logger [OPTIONS] directory prefix\n");
   printf ("       rotation_logger  --help|-h\n");
   printf ("       rotation_logger  --version|-v\n");
}

//------------------------------------------------------------------------------
//
static void printHelp()
{
   printf ("Rotation Logger v%s.\n"
           "\n"
           "This program provides a simple rotating logger. It is similar to tee, in that it\n"
           "copies from standard input to standard output and also to a log file. Unlike tee,\n"
           "the file size and/or file age is limited, and when the size or age exceeds the\n"
           "specified thresholds, a new file is created and output is directed to that file.\n"
           "\n", VERSION);

   printUsage ();

   printf ("\n"
           "Options\n"
           "\n"
           "--age,-a      age limit allowed for each file, expressed in seconds. It may be\n"
           "              qualifiedwith m, h, d or w for minutes, hours, days and weeks\n"
           "              respectively. The default is 1d. The value is forced to be >= 10s.\n"
           "\n"
           "--size,-s     size limit allowed for each file, expressed in bytes. It may be\n"
           "              qualified with K, M or G for kilo, mega and giga bytes respectively.\n"
           "              The default is 50M. The value is forced to be >= 20 bytes.\n"
           "\n"
           "--keep,k      number of files to keep. This is above and beyond the current file.\n"
           "              The default is 40. The value is forced to be >= 1.\n"
           "\n"
           "--help,-h     show this help information and exit.\n"
           "\n"
           "--version,-v  show program version and exit.\n"
           "\n"
           "\n"
           "Parameters\n"
           "\n"
           "directory     the location (relative or absolute) where the log files are to be\n"
           "              created. If the specified directory does not exists, it is created.\n"
           "              rotation_logger effectively does: mkdir -p '<directory>' on startup.\n "
           "\n"
           "prefix        this specifies the filename prefix given to the log files. The file\n"
           "              suffix is always \".log\". The full file filenames are of the form:\n"
           "\n"
           "              <directory>/<prefix>_YYYY-MM-DD_HH-MM-SS.log\n"
           "\n");
}

//------------------------------------------------------------------------------
// Credit: Jonathan Leffler
// http://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
//
static bool make_dir (const char* dirpath, const mode_t mode)
{
   struct stat st;
   bool status = true;


   if (stat (dirpath, &st) != 0) {
      // Directory does not exist. Test EEXIST for race condition.
      //
      if ((mkdir (dirpath, mode) != 0) && (errno != EEXIST)) {
         status = false;
         perrorf ("make_dir.1 (%s, 0x%04x)", dirpath, mode);
      }

   } else if (!S_ISDIR (st.st_mode)) {
      errno = ENOTDIR;
      status = false;
      perrorf ("make_dir.2 (%s, 0x%04x)", dirpath, mode);
   }

   return status;
}

//------------------------------------------------------------------------------
//
static bool mkdir_parents (const char* dirpath, mode_t const mode)
{
   if (!dirpath) return false;   // sainity check
   char* work_path = strdup (dirpath);
   if (!work_path) return false; // sainity check

   char* pp;
   char* sp;
   bool status;
// printf ("%s\n", dirpath);
   status = true;
   pp = work_path;
   while (status && (sp = strchr (pp, '/')) != 0) {
      if (sp != pp) {
         // Neither root nor double slash in path
         //
         *sp = '\0';
         status = make_dir (work_path, mode);
         *sp = '/';
      }
      pp = sp + 1;
   }
   if (status) {
      status = make_dir (dirpath, mode);
   }

   free (work_path);
   return status;
}

//------------------------------------------------------------------------------
// Filter directory entries looking for log files.
// We check prefix, suffix and length, we do not check the date part (yet).
//
static char thePrefix [FULL_PATH_LEN];   // includes prefix and date '_' separator.
static int prefixFilter (const struct dirent* entry)
{
   int result = 0;

   if (entry) {
      size_t pl;
      size_t dl;
      pl = strlen (thePrefix);
      dl  = strlen (entry->d_name);
///   printf ("%s  %ld  %ld\n", entry->d_name, dl, pl + 23);
      if (dl == pl + 23) {   // 23 <= "<date>.log"
         bool prefixOkay;
         bool suffixOkay;
         prefixOkay = strncmp (entry->d_name, thePrefix, pl) == 0;
         suffixOkay = strncmp (&entry->d_name [dl - 4], ".log", 4) == 0;
         if (prefixOkay &&  suffixOkay) result = 1;
      }
   }

   return result;
}

//------------------------------------------------------------------------------
//
static void purgeOldFiles (const char* directory, const char* prefix, const int numberToKeep)
{
   struct dirent** namelist;
   int n;
   int j;

   // Set up static prefix - imcluding the under score.
   //
   snprintf (thePrefix, sizeof (thePrefix), "%s_", prefix);

   n = scandir (directory, &namelist, prefixFilter, alphasort);
   if (n < 0) {
      perrorf ("scandir (%s)", directory);
      return;
   }

   for (j = 0; j < n; j++) {
      bool doPurge = j < (n - numberToKeep);
      if (doPurge) {
         char fullPath [FULL_PATH_LEN];
         int status;

         snprintf (fullPath, sizeof (fullPath), "%s/%s", directory, namelist[j]->d_name);
///      printf ("unlinking: %s\n", fullPath);
         status = unlink (fullPath);
         if (status < 0) {
            perrorf("unlink (%s)", fullPath);
         }
      }
      free (namelist[j]);
   }
   free (namelist);
}

//------------------------------------------------------------------------------
//
static int nextFile (const char* directory, const char* prefix, const int numberToKeep)
{
   time_t timeNow;
   char timeImage [24];
   char filename [FULL_PATH_LEN];

   // First unlink (delete) all but latest numberToKeep log files.
   //
   purgeOldFiles (directory, prefix, numberToKeep);

   time (&timeNow);
   strftime (timeImage, sizeof (timeImage), "%Y-%m-%d_%H-%M-%S", localtime (&timeNow));
   snprintf (filename,  sizeof (filename),  "%s/%s_%s.log", directory, prefix, timeImage);

   int fd = creat (filename, 0644);

   if (fd < 0) {
      perrorf ("creat(%s,0644)", filename);
   } else {
///   printf ("new log file: %s\n", filename);
   }

   return fd;
}

//------------------------------------------------------------------------------
//
int main (int argc, char** argv)
{
   // Default option values.
   //
   size_t sizeLimit = 50 * 1000 * 1000;   // 50M
   int ageLimit = 24 * 3600;              // 1 day
   int numberToKeep  = 40;                // in addition to the current file.

   // Process arguments
   //
   while (true) {
      int option_index = 0;
      size_t value = 0;

      // All long options also have a short option
      //
      static const struct option long_options[] = {
         {"help", no_argument, NULL, 'h'},
         {"version", no_argument, NULL, 'v'},
         {"age", required_argument, NULL, 'a'},
         {"size", required_argument, NULL, 's'},
         {"keep", required_argument, NULL, 'k'},
         {NULL, 0, NULL, 0}
      };

      const int c = getopt_long (argc, argv, "hva:s:k:", long_options, &option_index);
      if (c == -1)
         break;

      switch (c) {

         case 0:
            // All long options also have a short option
            //
            printf ("unexpected option %s", long_options[option_index].name);
            if (optarg)
               printf (" with arg %s", optarg);
            printf ("\n");
            printUsage();
            return 2;
            break;

         case 'h':
            printHelp ();
            return 0;
            break;

         case 'v':
            printf ("rotation_logger version %s\n", VERSION);
            return 0;
            break;

         case 'a':
            {
               char xx = ' ';
               int n = sscanf (optarg, "%ld%c", &value, &xx);
               if (n < 1) {
                  printUsage ();
                  return 1;
               }
               if (n == 2) {
                  if (xx == ' ') {
                  // do nothing
                  } else if (xx == 'm') {
                     value *= 60;
                  } else if (xx == 'h') {
                     value *= 3600;
                  } else if (xx == 'd') {
                     value *= 86400;
                  } else if (xx == 'w') {
                     value *= 604800;
                  } else {
                     printf ("usage - age limit modifier %c\n", xx);
                     printUsage();
                     return 1;
                  }
               }
               ageLimit = value;
            }
            break;

         case 's':
            {
               char xx = ' ';
               int n = sscanf (optarg, "%ld%c", &value, &xx);
               if (n < 1) {
                  printUsage ();
                  return 1;
               }
               if (n == 2) {
                  if (xx == ' ') {
                     // do nothing
                  } else if (xx == 'K') {
                     value *= 1000;
                  } else if (xx == 'M') {
                     value *= 1000000;
                  } else if (xx == 'G') {
                     value *= 1000000000;
                  } else {
                     printf ("usage - bad size modifier %c\n", xx);
                     printUsage ();
                     return 1;
                  }
               }
               sizeLimit = value;
            }
            break;

         case 'k':
            numberToKeep = atoi (optarg);
            break;

         case '?':
            // invalid option
            //
            printUsage();
            return 1;
            break;

         default:
            printf ("?? getopt returned character code 0%o ??\n", c);
            break;
      }
   }

   // Santise options: 10 second minimum, 20 bytes minimum, 
   // number file minimum is 2 (1 + current)
   //
   if (ageLimit < 10) {
      ageLimit = 10;
   }
   if (sizeLimit < 20) {
      sizeLimit = 20;
   }
   if (numberToKeep < 1) {
      numberToKeep = 1;
   }

   int numberArgs = argc - optind;
   if (numberArgs < 2) {
      printf ("missing arguments\n");
      printUsage();
      return 1;
   }

   const char* directory = argv[optind++];
   const char* prefix    = argv[optind++];

   printf ("Rotation Logger %s/%s\n", directory, prefix);
   printf ("age limit:  %d secs\n", ageLimit);
   printf ("size limit: %ld bytes\n", sizeLimit);
   printf ("keep:       %d\n", numberToKeep);

   bool okay = mkdir_parents (directory, 0755);
   if (!okay) {
      perrorf ("mkdir (%s,0755)", directory);
      return 2;
   }

   int fd;
   fd = nextFile (directory, prefix, numberToKeep);
   if (fd < 0) {
      return 2;
   }

   time_t lastTime;
   time (&lastTime);

   size_t total = 0;
   char last_char = '\n';
   ssize_t numberRead;

   while (true) {
      char buffer [2000];

      // cribbed from tee
      //
      numberRead = read (STDIN_FILENO, buffer, sizeof (buffer));
      if (numberRead < 0 && errno == EINTR)
         continue;
      if (numberRead <= 0)
         break; // end of input

      // First copy to standared output and write to current file.
      //
      int m1 = write (STDOUT_FILENO, buffer, numberRead);
      int m2 = write (fd, buffer, numberRead);
      total += m2;

      if (m1 != m2) {
         char message [120];
         int n = snprintf (message, sizeof (message),
                           "*** write mis-match %d/%d\n", m1, m2);
         write (STDERR_FILENO, message, n);
      }

      if (numberRead > 0) {
         last_char = buffer [numberRead - 1];
      }

      // Is a new file required? This is based on size and/or age of file,
      // To avoid name clash, the minimum allowed age is 1 second,
      //
      time_t thisTime;
      time (&thisTime);

      const time_t age = thisTime - lastTime;

      if ((age >= ageLimit) || ((total >= sizeLimit) && (age >= 1))) {
         // Ensure each file has a newline at the end.
         //
         if (last_char != '\n') {
            static const char newline [2] = "\n";
            write (fd, newline, 1);
         }
         close (fd);
         fd = nextFile (directory, prefix, numberToKeep);
         time (&lastTime);
         total = 0;
      }
   }

   if (numberRead == -1) {
      perrorf ("read error");
   }

   close (fd);
///   printf ("Rotation Logger complete\n");
   return 0;
}

/* end */
