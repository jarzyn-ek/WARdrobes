#include "main.h"
#include "watek_komunikacyjny.h"
#include "watek_glowny.h"
#include "monitor.h"
/* wątki */
#include <pthread.h>

/* sem_init sem_destroy sem_post sem_wait */
//#include <semaphore.h>
/* flagi dla open */
//#include <fcntl.h>

state_t stan=InRun;
volatile char end = FALSE;
int size,rank, tallow, free_lifts, reserved_lifts, free_rooms, reserved_rooms; /* nie trzeba zerować, bo zmienna globalna statyczna */
MPI_Datatype MPI_PAKIET_T;
pthread_t threadKom, threadMon;
int lamport_clock;

pthread_mutex_t stateMut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tallowMut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t liftMut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t roomMut = PTHREAD_MUTEX_INITIALIZER;


void check_thread_support(int provided)
{
    printf("THREAD SUPPORT: %d\n", provided);
    switch (provided) {
        case MPI_THREAD_SINGLE: 
            printf("Brak wsparcia dla wątków, kończę\n");
            /* Nie ma co, trzeba wychodzić */
	    fprintf(stderr, "Brak wystarczającego wsparcia dla wątków - wychodzę!\n");
	    MPI_Finalize();
	    exit(-1);
	    break;
        case MPI_THREAD_FUNNELED: 
            printf("tylko te wątki, ktore wykonaly mpi_init_thread mogą wykonać wołania do biblioteki mpi\n");
	    break;
        case MPI_THREAD_SERIALIZED: 
            /* Potrzebne zamki wokół wywołań biblioteki MPI */
            printf("tylko jeden watek naraz może wykonać wołania do biblioteki MPI\n");
	    break;
        case MPI_THREAD_MULTIPLE: printf("Pełne wsparcie dla wątków\n");
	    break;
        default: printf("Nikt nic nie wie\n");
    }
}

/* srprawdza, czy są wątki, tworzy typ MPI_PAKIET_T
*/
void inicjuj(int *argc, char ***argv)
{
    int provided;
    MPI_Init_thread(argc, argv,MPI_THREAD_MULTIPLE, &provided);
    check_thread_support(provided);


    /* Stworzenie typu */
    /* Poniższe (aż do MPI_Type_commit) potrzebne tylko, jeżeli
       brzydzimy się czymś w rodzaju MPI_Send(&typ, sizeof(pakiet_t), MPI_BYTE....
    */
    /* sklejone z stackoverflow */
    const int nitems=3; /* bo packet_t ma trzy pola */
    int       blocklengths[3] = {1,1,1};
    MPI_Datatype typy[3] = {MPI_INT, MPI_INT, MPI_INT};

    MPI_Aint     offsets[3]; 
    offsets[0] = offsetof(packet_t, ts);
    offsets[1] = offsetof(packet_t, src);
    offsets[2] = offsetof(packet_t, data);

    MPI_Type_create_struct(nitems, blocklengths, offsets, typy, &MPI_PAKIET_T);
    MPI_Type_commit(&MPI_PAKIET_T);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    srand(rank);

    pthread_create( &threadKom, NULL, startKomWatek , 0);
    if (rank==0) {
	pthread_create( &threadMon, NULL, startMonitor, 0);
    }
    debug("jestem");
}

/* usunięcie zamkków, czeka, aż zakończy się drugi wątek, zwalnia przydzielony typ MPI_PAKIET_T
   wywoływane w funkcji main przed końcem
*/
void finalizuj()
{
    pthread_mutex_destroy( &stateMut);
    /* Czekamy, aż wątek potomny się zakończy */
    println("czekam na wątek \"komunikacyjny\"\n" );
    pthread_join(threadKom,NULL);
    if (rank==0) pthread_join(threadMon,NULL);
    MPI_Type_free(&MPI_PAKIET_T);
    MPI_Finalize();
}


void sendPacket(packet_t *pkt, int destination, int tag)
{
    int freepkt=0;
    if (pkt==0) { pkt = malloc(sizeof(packet_t)); freepkt=1;}
    pkt->src = rank;
    MPI_Send( pkt, 1, MPI_PAKIET_T, destination, tag, MPI_COMM_WORLD);
    if (freepkt) free(pkt);
}

void changeTallow( int newTallow )
{
    pthread_mutex_lock( &tallowMut );
    if (stan==InFinish) { 
	pthread_mutex_unlock( &tallowMut );
        return;
    }
    //tallow += newTallow;
    debug("\n tallow zmieniony z %d ",tallow); 
    tallow+=newTallow;
    debug("na %d",tallow);
    pthread_mutex_unlock( &tallowMut );
}

void changeRooms( int rooms )
{
    pthread_mutex_lock( &roomMut );
    if (stan==InFinish) { 
	pthread_mutex_unlock( &roomMut );
        return;
    }

    debug("\n free_rooms zmienione z %d ",free_rooms); 
    free_rooms += rooms;
    debug("na %d",free_rooms);
    debug("\n reserved_rooms zmienione z %d ",reserved_rooms);
    reserved_rooms -= rooms;
    debug("na %d",reserved_rooms);
    pthread_mutex_unlock( &roomMut );
}

void changeLifts( int lifts )
{
    pthread_mutex_lock( &liftMut );
    if (stan==InFinish) { 
	pthread_mutex_unlock( &liftMut );
        return;
    }

    debug("\n free_lifts zmienione z %d ",free_lifts); 
    free_lifts += lifts;
    debug("na %d",free_lifts);
    debug("\n reserved_lifts zmienione z %d ",reserved_lifts);
    reserved_lifts -= lifts;
    debug("na %d",reserved_lifts);
    pthread_mutex_unlock( &liftMut );
}

int main(int argc, char **argv)
{
    /* Tworzenie wątków, inicjalizacja itp */
    inicjuj(&argc,&argv); // tworzy wątek komunikacyjny w "watek_komunikacyjny.c"
    tallow = 1000; // by było wiadomo ile jest łoju
    free_lifts = 500;
    reserved_lifts = 0;
    free_rooms = 1000;
    reserved_rooms = 0;
    mainLoop();          // w pliku "watek_glowny.c"

    finalizuj();
    return 0;
}

