/*************************************
 * VR473622, VR474954
 * Matteo Zandonai, Francesco Merighi
 * 09/12/2024
 *************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/signal.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>

#define SHM_KEY 4321
#define SEM_KEY 8765
#define SIZE 1024

#define P1_SYMBOL 0
#define P2_SYMBOL 1
#define PID_SRV 2
#define PID1 3
#define PID2 4
#define TURN 5
#define STATUS 6
#define TIMEOUT 7
#define MATRIX_DIM 8
#define VALID_MOVE 9

char player1;
char player2;
int ctrl_count = 0;
int matrix_dim = 0;
int board_start = 10; // inizio della matrice in mem
int timeout = 0;

int semid;
int shmid;
int sem_val;
int *shared_memory;
struct sembuf sop = {0, 0, 0};

// Cancellazione del segmento di memoria
void cleanup()
{
    // Deallocazione memoria condivisa
    if (shmdt(shared_memory) == -1)
    {
        perror("Errore durante la deallocazione della memoria condivisa (shmdt)");
    }

    // Rimuove area di memoria condivisa
    if (shmctl(shmid, IPC_RMID, NULL) == -1)
    {
        perror("Errore durante la rimozione dell'area di memoria condivisa (shmctl)");
    }

    // Deallocazione semaforo
    if (semctl(semid, 0, IPC_RMID) == -1)
    {
        perror("Errore durante la deallocazione del semaforo (semctl)");
    }
    printf("\n");
}

// Generatore del bot
void sig_fork_generator(int sig)
{
    if (shared_memory[STATUS] != 2)
    {
        pid_t bot_pid = fork();
        if (bot_pid < 0)
        {
            perror("Errore bot");
            cleanup();
            exit(0);
        }
        else if (bot_pid == 0)
        {
            // exec
            execl("./TriClient", "./TriClient", "bot", (char *)NULL);
        }
        else
        {
            shared_memory[PID2] = bot_pid;
        }
    }
}

// Controllo iniziale
void startup_controls(int argc, char *argv[])
{
    if (argc < 4 || argc > 4)
    {
        printf("Utilizzo: ./TriServer <timeout> <simbolo 1> <simbolo 2>\n");
        printf("  Esempio: ./TriServer 10 O X\n");
        exit(0);
    }
    else
    {
        timeout = atoi(argv[1]);
        if (timeout < 0)
        {
            printf("Errore: il timer della mossa non può essere negativo.\n");
            printf("Utilizzo: ./TriServer <timeout> <simbolo 1> <simbolo 2>\n");
            printf("  Esempio: ./TriServer 10 O X\n");
            exit(0);
        }

        if (strlen(argv[2]) != 1 || strlen(argv[3]) != 1)
        {
            printf("Errore: formato del simbolo errato.\n");
            printf("Utilizzo: ./TriServer <timeout> <simbolo 1> <simbolo 2>\n");
            printf("  Esempio: ./TriServer 10 O X\n");
            exit(0);
        }
        else
        {
            player1 = argv[2][0];
            player2 = argv[3][0];
        }
    }

    printf("Player 1: %c\n", player1);
    printf("Player 2: %c\n", player2);
}

// Gestione del CTRL + C --> SIGTERM
void sig_handle_ctrl(int sig)
{
    if (ctrl_count == 0)
    {
        printf("\nHai premuto CTRL+C, premi di nuovo per terminare\n");
        ctrl_count++;
    }
    else
    {
        printf("----------------------------------------------------\n");
        printf("    G A M E   O V E R : Partita terminata\n");
        printf("----------------------------------------------------\n");
        // Rimozione
        if (kill(shared_memory[PID1], 0) == 0)
        {
            kill(shared_memory[PID1], SIGTERM);
        }

        if (kill(shared_memory[PID2], 0) == 0)
        {
            kill(shared_memory[PID2], SIGTERM);
        }

        cleanup();
        exit(0);
    }
}

// Gestore della chiusura del client
void sig_client_closed(int sig)
{
    printf("----------------------------------------------------\n");
    printf("    G A M E   O V E R : Un giocatore ha abbandonato\n");
    printf("----------------------------------------------------\n");
    if (kill(shared_memory[PID1], 0) == 0)
    {
        kill(shared_memory[PID1], SIGUSR1);
    }

    if (kill(shared_memory[PID2], 0) == 0)
    {
        kill(shared_memory[PID2], SIGUSR1);
    }

    cleanup();
    exit(0);
}

// Gestore del timer
void sig_client_timer(int sig)
{
    printf("-------------------------------------------------------\n");
    printf("    G A M E   O V E R : Un client ha perso per timeout\n");
    printf("-------------------------------------------------------\n");
    if (kill(shared_memory[PID1], 0) == 0 && shared_memory[TURN] != 0)
    {
        kill(shared_memory[PID1], SIGUSR2);
    }

    if (kill(shared_memory[PID2], 0) == 0 && shared_memory[TURN] != 1)
    {
        kill(shared_memory[PID2], SIGUSR2);
    }

    cleanup();
    exit(0);
}

// Controllo per la vittoria
bool victory()
{
    int dim = shared_memory[MATRIX_DIM];
    for (int i = 0; i < dim; i++)
    {
        // controllo righe
        bool row_victory = true;
        for (int j = 0; j < dim; j++)
        {

            if (shared_memory[board_start + i * dim + j] != shared_memory[board_start + i * dim] ||
                shared_memory[board_start + i * dim] == ' ')
            {
                row_victory = false;
                break;
            }
        }
        if (row_victory)
        {
            return true;
        }

        // controllo colonne
        bool col_victory = true;
        for (int j = 0; j < dim; j++)
        {
            if (shared_memory[board_start + j * dim + i] != shared_memory[board_start + i] ||
                shared_memory[board_start + i] == ' ')
            {
                col_victory = false;
                break;
            }
        }
        if (col_victory)
        {
            return true;
        }
    }

    // Controllo diagonale principale
    bool diag_victory1 = true;
    for (int i = 1; i < dim; i++) // Parto da 1 per confrontare con la prima cella
    {
        if (shared_memory[board_start + i * dim + i] != shared_memory[board_start] ||
            shared_memory[board_start] == ' ')
        {
            diag_victory1 = false;
            break;
        }
    }

    // Controllo diagonale secondaria
    bool diag_victory2 = true;
    for (int i = 1; i < dim; i++) // Parto da 1 per confrontare con la prima cella
    {
        if (shared_memory[board_start + i * dim + (dim - 1 - i)] != shared_memory[board_start + (dim - 1)] ||
            shared_memory[board_start + (dim - 1)] == ' ')
        {
            diag_victory2 = false;
            break;
        }
    }

    return diag_victory1 || diag_victory2;
}

// Controllo per il pareggio
bool draw()
{
    // controllo le celle occupate della matrice
    int dim = shared_memory[MATRIX_DIM];
    int board_sz = dim * dim;

    if (victory())
        return false;

    for (int i = 0; i < board_sz; i++)
    {
        if (shared_memory[board_start + i] == ' ')
        {
            return false;
        }
    }

    return true;
    // se tutte le celle sono occupate e nessuno ha visto c'è pareggio
}

// Main program
int main(int argc, char *argv[])
{
    signal(SIGINT, sig_handle_ctrl);
    signal(SIGUSR1, sig_client_closed);
    signal(SIGUSR2, sig_client_timer);
    signal(SIGTERM, sig_fork_generator); // Gestore gioco bot

    // Controllo iniziale dei parametri
    startup_controls(argc, argv);

    do
    {
        printf("\nInserisci la dimensione della matrice (min 3): ");
        scanf("%i", &matrix_dim);

        if (matrix_dim < 3)
        {
            printf("ERRORE - La dimensione del tabellone deve essere almeno 3\n");
        }

    } while (matrix_dim < 3);

    // Creazione memoria condivisa
    shmid = shmget(SHM_KEY, SIZE, IPC_CREAT | 0660);
    if (shmid == -1)
    {
        perror("shmget");
        exit(0);
    }

    shared_memory = (int *)shmat(shmid, NULL, SHM_RND);
    if (shared_memory == (int *)-1)
    {
        perror("shmat");
        exit(0);
    }

    /* inizializzazione shared memory */
    shared_memory[P1_SYMBOL] = player1;     // simbolo player1
    shared_memory[P2_SYMBOL] = player2;     // simbolo player2
    shared_memory[PID_SRV] = getpid();      // pid del server
    shared_memory[PID1] = 0;                // PID client1
    shared_memory[PID2] = 0;                // PID client2
    shared_memory[TURN] = 0;                // turno corrente (0 o 1)
    shared_memory[STATUS] = 0;              // stato del gioco (0, 1, 2, 3)
    shared_memory[TIMEOUT] = timeout;       // timeout
    shared_memory[MATRIX_DIM] = matrix_dim; // dimensione matrice
    shared_memory[VALID_MOVE] = 0;          // gestore della mossa valida

    // Init della matrice
    int board_sz = matrix_dim * matrix_dim;
    for (int i = 0; i < board_sz; i++)
    {
        shared_memory[board_start + i] = ' ';
    }

    // Creazione semaforo
    semid = semget(SEM_KEY, 1, IPC_CREAT | 0600);
    if (semid < 0)
    {
        perror("Errore nella crezione del semaforo");
        exit(0);
    }

    if (semctl(semid, 0, SETVAL, 0) == -1) // Imposto a 0 il semaforo
    {
        perror("Errore nell'assegnazione di 0 al semaforo");
        cleanup();
        exit(0);
    }

    // Il gioco va avanti fin quando un giocatore vince o pareggia
    shared_memory[STATUS] = 1; // Gioco iniziato
    printf("In attesa di due giocatori per iniziare la partita...\n");

    /*
    sop.sem_num = 0;
    sop.sem_op = -2;
    sop.sem_flg = 0;

    if (semop(semid, &sop, 1) == -1)
    {
        printf("\n");
        printf("Errore con il semaforo\n");
        printf("\n");
        if (kill(shared_memory[PID1], 0) == 0)
        {
            kill(shared_memory[PID1], SIGTERM);
        }

        if (kill(shared_memory[PID2], 0) == 0)
        {
            kill(shared_memory[PID2], SIGTERM);
        }

        check_sem(semid);
        cleanup();
        exit(0);
    }
    */

    while (semctl(semid, 0, GETVAL) < 2)
    {
        semctl(semid, 0, GETVAL);
    }

    printf("Due giocatori connessi...la parita ha inizio\n");

    while (1)
    {
        /*
            shared_memory[STATUS] = 0 // gioco da iniziare
            shared_memory[STATUS] = 1 // gioco iniziato
            shared_memory[STATUS6] = 2 // vittoria pareggio
            shared_memory[STATUS] = 3 // pareggio
        */

        if (shared_memory[VALID_MOVE] == 1)
        {
            if (victory())
            {
                // printf("Un giocatore ha vinto\n");
                shared_memory[STATUS] = 2; // Stato: Vittoria
                kill(shared_memory[PID1], SIGTERM);
                kill(shared_memory[PID2], SIGTERM);
                cleanup();
                exit(0);
            }
            else if (draw())
            {
                // printf("Pareggio!\n");
                shared_memory[STATUS] = 3; // Stato: pareggio
                kill(shared_memory[PID1], SIGTERM);
                kill(shared_memory[PID2], SIGTERM);
                cleanup();
                exit(0);
            }
            else
            {
                // Passa il turno all'altro giocatore
                shared_memory[TURN] = (shared_memory[TURN] == 0) ? 1 : 0;
                shared_memory[VALID_MOVE] = 0;
            }
        }
    }

    // Rimozione memoria, semaforo
    cleanup();
    return 0;
}