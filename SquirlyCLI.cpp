#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>
#include <AL/alut.h>

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

const char* TRANSACTION_FILE = "/etc/Squirly/squirly.transactions";
const char* LOG_FILE = "/etc/Squirly/squirly.log";

void handler(int sig)
{
	//printf("\nexiting...(%d)\n", sig);
	exit(0);
}

void perror_exit(char* error)
{
	perror(error);
	handler(9);
}

void log( const std::string& msg )
{
	std::ofstream log( LOG_FILE, std::ios_base::app );
	log << msg << std::endl;
	log.close();
}

/// @brief Used to map between key codes and ascii characters. The character at each position is the ascii equivalent of the key code
const char ASCII_CHARACTERS[] = "  12345678" "90-= 	QWER" "TYUIOP[]\n " "ASDFGHJKL;" "'` \\ZXCVBN" "M,./ *    " "          " " 789-456+1" "23        " "        // "; 

/// @brief Get the ascii character for the given key code
/// @param[in] code The code to convert
/// @returns The ascii equivalent of the key code (either capital letter, number or some sybols. Unrecognized or unprintable characters return the null character or a space, respectively
char getASCII( int code )
{
	if( code > 0 && code < (sizeof(ASCII_CHARACTERS)/sizeof(ASCII_CHARACTERS[0])) )
		return ASCII_CHARACTERS[code];
	else
		return '\0';

}

void playSound()
{
	ALuint helloBuffer, helloSource;
	helloBuffer = alutCreateBufferFromFile("sounds/deposited.wav");
	alGenSources(1, &helloSource);
	alSourcei(helloSource, AL_BUFFER, helloBuffer);
	alSourcePlay(helloSource);
	alutSleep(2);
	return;
}

/// @brief The default direction of food, true if food is, by default
/// taken from the store, false if by default it is added
const bool DEFAULT_ADD_ITEMS = false;

/// @brief Token used to indicate an object is added
static const char ADD_TOKEN[] = "ADD";
/// @brief Token used to indicate an object is taken
static const char TAKE_TOKEN[] = "TAKE";

/// @brief Does all the work for a transaction to be fully recorded
void commitTransaction( const std::string& upc )
{
	static time_t last_commit_time = 0;
	static bool is_adding = false;

	std::ostringstream log_msg;
	log_msg << "Received UPC '" << upc << "'" << std::endl;
	log( log_msg.str() );
	// Bail if we are getting erroneous characters that aren't a full UPC
	if( upc.length() != 12 ) {
		log("Discarding UPC for being too short"); 
		return;
	}

	// Get the current date and time
	time_t cur_time = ::time( 0 );
	tm* broken_time = ::localtime( &cur_time );
	char time_buffer[256];
	strftime( time_buffer, 256, "%F %H:%M:%S ", broken_time );

	// Determine if we are adding or subtracting
	if( upc == "100000000007" ) {
		log("Got take command");
		is_adding = false;
		last_commit_time = cur_time;
		return;
	}
	else if( upc == "200000000004" ) {
		log("Got add command");
		is_adding = true;
		last_commit_time = cur_time;
		return;
	}
	else {
		double time_since_last_commit_seconds = ::difftime( cur_time, last_commit_time );
		std::ostringstream log_msg;
		log_msg << "Time since last commit: " << time_since_last_commit_seconds << " seconds" << std::endl;
		log(log_msg.str());
		if( time_since_last_commit_seconds > 60 * 10 ) {
			is_adding = DEFAULT_ADD_ITEMS;
			log("reverting back to add");
		}
	}
	last_commit_time = cur_time;
	
	std::ostringstream log_msg2;
	log_msg2 << "Current action is add: " << is_adding;
	log( log_msg2.str() );
	// Open the transaction file
	std::ofstream f( TRANSACTION_FILE, std::ios_base::app );
	// Add the timestamp and upc to the file
	f << time_buffer << upc << " ";
	// Add the direction
	if( is_adding )
		f << ADD_TOKEN;
	else
		f << TAKE_TOKEN;
	// flush the buffer and close
	f << std::endl;
	f.close();

	// Play the sound
	playSound();
}

/// @brief Cause this process to become a daemon
void daemonize() {
	int return_code = daemon(0, 0);
	if( return_code != 0 ) {
		log( "Failed to daemonize" );
		exit(3);
	}
}

/// @brief The main function of the program
int main(int argc, char* argv[])
{
	alutInit(&argc, argv);

	bool run_daemon = true;
	// parse the commandline args
	for( int i = 0; i < argc; ++i ) {
		if( argv[i] == "--no-daemon" )
			run_daemon = false;
	}

	// Start the daemon, if necessary
	if( run_daemon )
		daemonize();

	// Setup check
	if( argv[1] == NULL ) {
		log( "Please specify the path to the device" );
		exit(0);
	}

	// Get the device
	char* device = NULL;
	if( argc > 1 )
		device = argv[1];

	// See if the device exists at all
	struct stat file_info;
	int return_code = ::stat( device, &file_info );
	if( return_code != 0 ) {
		std::ostringstream buffer;
		buffer << device <<  " is not a valid device. Waiting until the device becomes valid...";
		log( buffer.str() );
		while( ::stat( device, &file_info ) != 0 )
		{
			::sleep( 1 );
		}
	}

	// Open Device
	int file_descriptor = 0;
	if( ( file_descriptor = open(device, O_RDONLY) ) == -1 )
	{
		std::ostringstream buffer;
		buffer << device << " cannot be opened.";
		log( buffer.str() );
		if( getuid() != 0 )
			log( " Try root permissions." );
		exit(2);
	}

	// Print Device Name
	char name[256] = "Unknown";
	ioctl( file_descriptor, EVIOCGNAME (sizeof (name)), name );
	std::ostringstream log_buffer;
	log_buffer << "Reading from " << device << " (" << name << ")" << std::endl;
	log( log_buffer.str() );
	
	// Read events
	struct input_event events[64];	
	std::ostringstream buffer;
	while(1) {
		int bytes_read = read(file_descriptor, events, sizeof(input_event) * 64);
		int events_read = bytes_read / sizeof(input_event);

		if( events_read <= 0 ) {
			log( "Failed to read any events" );
			exit( 0 );
		}
		for( int i = 0; i < events_read; ++i ) {
			input_event* current_event = events + i;
			// Ignore all but key-down events:
			std::ostringstream log_msg2;
			log_msg2 << "Got event type " << current_event->type << " value " << current_event->value << " code " << current_event->code << std::endl;
			log( log_msg2.str() );
			if( current_event->type == EV_KEY ) {
				if( current_event->value == 1 ) {
					char c = getASCII( current_event->code );	
					std::ostringstream log_msg;
					log_msg << "From scanner '" << c << "'" << std::endl;
					log(log_msg.str());
					 if( c == '\n' ) {
						commitTransaction( buffer.str() );
						buffer.str("");
						buffer.clear();
					} else if ( c == ' ' ) {
						//printf( "Event: type %d code %d value %d:   %c\n", current_event->type, current_event->code, current_event->value, getASCII( current_event->code ) );
					} else {
						buffer << c;
					}
					
				}
			}
			
		}
	}

	alutExit();
	return 0;
}
