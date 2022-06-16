/* rotation_logger.c
 * 
 * Copyright (C) 2019-2022  Andrew C. Starritt
 * All rights reserved.
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
#define VERSION          "1.1.8"

/*------------------------------------------------------------------------------
 */
static void perrorf (const char* format, ...)
{
   char message [240];
   va_list arguments;
   va_start (arguments, format);
   vsnprintf (message, sizeof (message), format, arguments);
   va_end (arguments);
   perror (message);
}

/*------------------------------------------------------------------------------
 */
static void printWarranty()
{
   printf ("[31;1mrotation_logger[00m is distributed under the "
           "GNU General Public License version 3.\n"
           "\n"
           "Disclaimer of Warranty and Limitation of Liability.\n"
           "\n"
           "  THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY\n"
           "APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT\n"
           "HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM [38;1m\"AS IS\"[00m WITHOUT WARRANTY\n"
           "OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,\n"
           "THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR\n"
           "PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM\n"
           "IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF\n"
           "ALL NECESSARY SERVICING, REPAIR OR CORRECTION.\n"
           "\n"
           "  IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING\n"
           "WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MODIFIES AND/OR CONVEYS\n"
           "THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY\n"
           "GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE\n"
           "USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO LOSS OF\n"
           "DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD\n"
           "PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER PROGRAMS),\n"
           "EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF\n"
           "SUCH DAMAGES.\n"
           "\n");
}

/*------------------------------------------------------------------------------
 */
static void printUsage()
{
   printf ("usage: rotation_logger [OPTIONS] directory prefix\n");
   printf ("       rotation_logger  --help|-h\n");
   printf ("       rotation_logger  --version|-v\n");
   printf ("       rotation_logger  --warranty|-w\n");
}

/*------------------------------------------------------------------------------
 */
static void printHelp()
{
   printf ("Rotation Logger v%s\n"
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
           "              qualified with m, h, d or w for minutes, hours, days and weeks\n"
           "              respectively. The default is 1d. The age is constrained to be >= 10s.\n"
           "\n"
           "--size,-s     size limit allowed for each file, expressed in bytes. It may be\n"
           "              qualified with K, M or G for kilo, mega and giga bytes respectively.\n"
           "              The default is 50M. The size is constrained to be >= 20 bytes.\n"
           "\n"
           "--keep,k      number of files to keep. This is above and beyond the current file.\n"
           "              The default is 40. The keep value is constrained to be >= 1.\n"
           "\n"
           "--help,-h     show this help information and exit.\n"
           "\n"
           "--warranty,-w show warranty information and exit.\n"
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

/*------------------------------------------------------------------------------
 * Credit: Jonathan Leffler
 * http://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
 */
static bool make_dir (const char* dirpath, const mode_t mode)
{
   struct stat st;
   bool status = true;

   if (stat (dirpath, &st) != 0) {
      /* Directory does not exist. Test EEXIST for race condition.
       */
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

/*------------------------------------------------------------------------------
 */
static bool mkdir_parents (const char* dirpath, mode_t const mode)
{
   char* work_path;
   char* pp;
   char* sp;
   bool status;
	 
   if (!dirpath) return false;   /* sainity check */
   work_path = strdup (dirpath);
   if (!work_path) return false; /* sainity check */

/* printf ("%s\n", dirpath); */
   status = true;
   pp = work_path;
   while (status && (sp = strchr (pp, '/')) != 0) {
      if (sp != pp) {
         /* Neither root nor double slash in path
          */
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

/*------------------------------------------------------------------------------
 * Filter directory entries looking for log files.
 * We check prefix, suffix and length, we do not check the date part (yet).
 */
static char thePrefix [FULL_PATH_LEN];   /* includes prefix and date '_' separator. */
static int prefixFilter (const struct dirent* entry)
{
   int result = 0;

   if (entry) {
      size_t pl;
      size_t dl;
      pl = strlen (thePrefix);
      dl  = strlen (entry->d_name);
/**   printf ("%s  %ld  %ld\n", entry->d_name, dl, pl + 23);  **/
      if (dl == pl + 23) {   /* 23 <= "<date>.log" */
         bool prefixOkay;
         bool suffixOkay;
         prefixOkay = strncmp (entry->d_name, thePrefix, pl) == 0;
         suffixOkay = strncmp (&entry->d_name [dl - 4], ".log", 4) == 0;
         if (prefixOkay &&  suffixOkay) result = 1;
      }
   }

   return result;
}

/*------------------------------------------------------------------------------
 */
static void purgeOldFiles (const char* directory, const char* prefix, const int numberToKeep)
{
   struct dirent** namelist;
   int n;
   int j;

   /* Set up static prefix - imcluding the under score.
    */
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
/**      printf ("unlinking: %s\n", fullPath);  **/
         status = unlink (fullPath);
         if (status < 0) {
            perrorf("unlink (%s)", fullPath);
         }
      }
      free (namelist[j]);
   }
   free (namelist);
}

/*------------------------------------------------------------------------------
 */
static int nextFile (const char* directory, const char* prefix, const int numberToKeep)
{
   time_t timeNow;
   char timeImage [24];
   char filename [FULL_PATH_LEN];
   int fd;

   /* First unlink (delete) all but latest numberToKeep log files.
    */
   purgeOldFiles (directory, prefix, numberToKeep);

   time (&timeNow);
   strftime (timeImage, sizeof (timeImage), "%Y-%m-%d_%H-%M-%S", localtime (&timeNow));
   snprintf (filename,  sizeof (filename),  "%s/%s_%s.log", directory, prefix, timeImage);

   fd = creat (filename, 0644);
   if (fd < 0) {
      perrorf ("creat(%s,0644)", filename);
   } else {
/**   printf ("new log file: %s\n", filename); **/
   }

   return fd;
}

/*------------------------------------------------------------------------------
 */
int main (int argc, char** argv)
{
   /* Default option values.
    */
   long sizeLimit = 50 * 1000 * 1000;   /* 50M */
   int ageLimit = 24 * 3600;            /* 1 day */
   int numberToKeep  = 40;              /* in addition to the current file. */
   int numberArgs;
   char* directory = NULL;
   char* prefix    = NULL;
   bool okay;
   int fd;
   time_t lastTime;
   size_t total;
   char last_char;
   ssize_t numberRead;

   /* Process arguments
    */
   while (true) {

      /* All long options also have a short option
       */
      static const struct option long_options[] = {
         {"help", no_argument, NULL, 'h'},
         {"version", no_argument, NULL, 'v'},
         {"warranty", no_argument, NULL, 'w'},
         {"age", required_argument, NULL, 'a'},
         {"size", required_argument, NULL, 's'},
         {"keep", required_argument, NULL, 'k'},
         {NULL, 0, NULL, 0}
      };

      int option_index = 0;
      long value = 0;
      const int c = getopt_long (argc, argv, "hvwa:s:k:", long_options, &option_index);
      
      if (c == -1)
         break;

      switch (c) {

         case 0:
            /* All long options also have a short option
             */
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

         case 'w':
            printWarranty();
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
                  /* do nothing */
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
                     /* do nothing */
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
            /* invalid option
             */
            printUsage();
            return 1;
            break;

         default:
            printf ("?? getopt returned character code 0%o ??\n", c);
            break;
      }
   }

   fprintf (stderr, "This program comes with ABSOLUTELY NO WARRANTY, "
            "for details run 'rotation_logger --warranty'.\n");

   /* Santise options: 10 second minimum, 20 bytes minimum, 
    * number file minimum is 2 (1 + current)
    */
   if (ageLimit < 10) {
      ageLimit = 10;
   }
   if (sizeLimit < 20) {
      sizeLimit = 20;
   }
   if (numberToKeep < 1) {
      numberToKeep = 1;
   }

   numberArgs = argc - optind;
   if (numberArgs < 2) {
      printf ("missing arguments\n");
      printUsage();
      return 1;
   }

   directory = argv[optind++];
   prefix    = argv[optind++];

   /* User messages need to be sent to stderr.
    */
   fprintf (stderr, "Rotation Logger %s/%s\n", directory, prefix);
   fprintf (stderr, "age limit:  %d secs\n", ageLimit);
   fprintf (stderr, "size limit: %ld bytes\n", sizeLimit);
   fprintf (stderr, "keep:       %d\n", numberToKeep);

   okay = mkdir_parents (directory, 0755);
   if (!okay) {
      perrorf ("mkdir (%s,0755)", directory);
      return 2;
   }

   fd = nextFile (directory, prefix, numberToKeep);
   if (fd < 0) {
      return 2;
   }
    
   time (&lastTime);
   total = 0;
   last_char = '\n';

   while (true) {
      char buffer [2000];
      int m1;
      int m2;
      time_t thisTime;
      time_t age;

      /* cribbed from tee
       */
      numberRead = read (STDIN_FILENO, buffer, sizeof (buffer));
      if (numberRead < 0 && errno == EINTR)
         continue;
      if (numberRead <= 0)
         break; /* end of input */

      /* First copy to standared output and write to current file.
       */
      m1 = write (STDOUT_FILENO, buffer, numberRead);
      m2 = write (fd, buffer, numberRead);
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

      /* Is a new file required? This is based on size and/or age of file,
       * To avoid name clash, the minimum allowed age is 1 second,
       */
      time (&thisTime);
      age = thisTime - lastTime;

      if ((age >= ageLimit) || ((total >= sizeLimit) && (age >= 1))) {
         /* Ensure each file has a newline at the end.
          */
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
/**   printf ("Rotation Logger complete\n");  **/
   return 0;
}

/* end */
