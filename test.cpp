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

void handler(int sig)
{
	printf("\nexiting...(%d)\n", sig);
	exit(0);
}

void perror_exit(char* error)
{
	perror(error);
	handler(9);
}

int main(int argc, char* argv[])
{
	// Setup check
	if( argv[1] == NULL ) {
		printf( "Please specify the path to the device" );
		exit(0);
	}

	char* device = NULL;
	if( argc > 1 )
		device = argv[1];

	// See if the device exists at all
	struct stat file_info;
	int return_code = ::stat( device, &file_info );
	if( return_code != 0 ) {
		printf( "%s is not a valid device.\n", device );
		exit(1);
	}

	// Open Device
	int file_descriptor = 0;
	if( ( file_descriptor = open(device, O_RDONLY) ) == -1 )
	{
		printf( "%s cannot be opened.\n", device );
		if( getuid() != 0 )
			printf( " Try root permissions.\n" );
		exit(2);
	}

	// Print Device Name
	char name[256] = "Unknown";
	ioctl( file_descriptor, EVIOCGNAME (sizeof (name)), name );
	printf( "Reading from %s (%s)\n", device, name );
	
	// Read events
	struct input_event events[64];	
	while(1) {
		int bytes_read = read(file_descriptor, events, sizeof(input_event) * 64);
		int events_read = bytes_read / sizeof(input_event);

		if( events_read <= 0 ) {
			printf( "Failed to read any events\n" );
			exit( 0 );
		}
		for( int i = 0; i < events_read; ++i ) {
			input_event* current_event = events + i;
			printf( "Event: %d %d %d\n", current_event->type, current_event->code, current_event->value );
		}
		printf("---------\n");
	}

	return 0;
}
