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

std::ofstream log;

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

/// @brief Does all the work for a transaction to be fully recorded
void commitTransaction( const std::string& upc )
{
	
	// Get the current date and time
	time_t cur_time = ::time( 0 );
	tm* broken_time = ::localtime( &cur_time );
	char time_buffer[256];
	strftime( time_buffer, 256, "%F %H:%M:%S ", broken_time );

	// Open the transaction file
	std::ofstream f( TRANSACTION_FILE, std::ios_base::app );
	f << time_buffer << upc << std::endl;
	f.close();

	// Play the sound
	playSound();
}

/// @brief Cause this process to become a daemon
void daemonize() {
	int return_code = daemon(0, 0);
	if( return_code != 0 ) {
		log << "Failed to daemonize" << std::endl;
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

	// Open the log file
	log.open( LOG_FILE, std::ios_base::app );

	// Setup check
	if( argv[1] == NULL ) {
		log <<  "Please specify the path to the device" << std::endl;
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
		log << device <<  " is not a valid device. Waiting until the device becomes valid..." << std::endl;
		while( ::stat( device, &file_info ) != 0 )
		{
			::sleep( 1 );
		}
	}

	// Open Device
	int file_descriptor = 0;
	if( ( file_descriptor = open(device, O_RDONLY) ) == -1 )
	{
		log << device << " cannot be opened." << std::endl;
		if( getuid() != 0 )
			log << " Try root permissions." << std::endl;
		exit(2);
	}

	// Print Device Name
	char name[256] = "Unknown";
	ioctl( file_descriptor, EVIOCGNAME (sizeof (name)), name );
	log << "Reading from " << device << " (" << name << ")" << std::endl;
	
	// Read events
	struct input_event events[64];	
	std::ostringstream buffer;
	while(1) {
		int bytes_read = read(file_descriptor, events, sizeof(input_event) * 64);
		int events_read = bytes_read / sizeof(input_event);

		if( events_read <= 0 ) {
			log << "Failed to read any events" << std::endl;
			exit( 0 );
		}
		for( int i = 0; i < events_read; ++i ) {
			input_event* current_event = events + i;
			// Ignore all but key-down events:
			if( current_event->type == EV_KEY ) {
				if( current_event->value == 1 ) {
					char c = getASCII( current_event->code );
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
