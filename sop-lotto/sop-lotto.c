#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "utils.h"

void usage(char *name);
void parent_work(int gamers, int weeks, int toPlayer[][2], int fromPlayer[][2]);
void create_children(int gamers, int weeks, int toPlayer[][2], int fromPlayer[][2]);
void child_work(int weeks, int readPipe, int writePipe);

int main(int argc, char **argv)
{
    // Assigning values
    int gamers;
    int weeks;
    if (argc != 3)
    {
        usage(argv[0]);
    }
    gamers = atoi(argv[1]);
    weeks = atoi(argv[2]);
    if (gamers <= 0 || weeks <= 0)
    {
        usage(argv[0]);
    }

    // Setting handler for SIGPIPE
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("sethandler: ");

    // Creating pipes
    int toPlayer[gamers][2];
    int fromPlayer[gamers][2];
    create_pipes(gamers, toPlayer);
    create_pipes(gamers, fromPlayer);

    // Creating all players
    create_children(gamers, weeks, toPlayer, fromPlayer);

    // Closing file descriptors and starting totalizator work
    for (int i = 0; i < gamers; i++)
    {
        close_pipe(toPlayer[i][READ]);
        close_pipe(fromPlayer[i][WRITE]);
    }
    parent_work(gamers, weeks, toPlayer, fromPlayer);

    // Closing the rest of file descriptors and waiting for child processes
    for (int i = 0; i < gamers; i++)
    {
        if (toPlayer[i][WRITE] != 0)
            close_pipe(toPlayer[i][WRITE]);
        if (fromPlayer[i][READ] != 0)
            close_pipe(fromPlayer[i][READ]);
    }
    while (wait(NULL) > 0)
        ;

    exit(EXIT_SUCCESS);
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: N >= 1 - number of players\n");
    fprintf(stderr, "T: T >= 1 - number of weeks (iterations)\n");
    exit(EXIT_FAILURE);
}

void create_children(int gamers, int weeks, int toPlayer[][2], int fromPlayer[][2])
{
    int n = gamers;

    for (int i = 0; i < n; i++)
    {
        switch(fork())
        {
            case 0:
                close_except(toPlayer, n, i, READ);
                close_except(fromPlayer, n, i, WRITE);
                int readPipe = toPlayer[i][READ];
                int writePipe = fromPlayer[i][WRITE];

                child_work(weeks, readPipe, writePipe);
                
                close_pipe(readPipe);
                close_pipe(writePipe);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork: ");
        }
    }
}

void child_work(int weeks, int readPipe, int writePipe)
{
    srand((unsigned)time(NULL) * getpid());
    int pid = getpid();
    int ret;
    int *bet = (int*)malloc(sizeof(int) * NUMBERS);
    int *lotto = (int*)malloc(sizeof(int) * NUMBERS);
    
    for (int i = 0; i < weeks; i++)
    {
        if ((rand() % 10) == 0)
        {
            printf("[%d] This is a waste of money\n", pid);
            break;
        }
        draw(bet);

        if ((ret = write(writePipe, &pid, sizeof(int))) < 0)
            ERR("write: ");
        if ((ret = write(writePipe, bet, sizeof(int) * NUMBERS)) < 0)
            ERR("write: ");
        if ((ret = read(readPipe, lotto, sizeof(int) * NUMBERS)) < 0)
            ERR("read: ");
        
        int result = compare(bet, lotto);
        int win = get_reward(result);
        printf("[%d] I won: %d zl\n", pid, win);
    }

    free(bet);
    free(lotto);
}

void parent_work(int gamers, int weeks, int toPlayer[][2], int fromPlayer[][2])
{
    srand((unsigned)time(NULL) * getpid());
    int n = gamers;
    int leftPlayers = gamers;
    int pid, ret;
    int totalReward = 0;
    int totalBet = 0;
    int *lotto = (int*)malloc(sizeof(int) * NUMBERS);
    int *bet = (int*)malloc(sizeof(int) * NUMBERS);
    
    for (int k = 0; k < weeks; k++)
    {
        draw(lotto);
        if (leftPlayers == 0)
        {
            break;
        }

        for (int i = 0; i < n; i++)
        {
            if (fromPlayer[i][READ] == 0)
            {
                continue;
            }

            if ((ret = read(fromPlayer[i][READ], &pid, sizeof(int))) < 0)
                ERR("read: ");
            if (ret == 0)
            {
                close_pipe(fromPlayer[i][READ]);
                fromPlayer[i][READ] = 0;
                leftPlayers--;
                continue;
            }

            if ((ret = read(fromPlayer[i][READ], bet, sizeof(int) * NUMBERS)) < 0)
                ERR("read: ");

            printf("Totalizator sportowy: [%d] bet: ", pid);
            for (int j = 0; j < NUMBERS; j++)
            {
                printf("%d, ", bet[j]);
            }
            printf("\n");
            int result = compare(bet, lotto);
            int win = get_reward(result);
            totalBet += BET;
            totalReward += win;
            errno = 0;

            if ((ret = write(toPlayer[i][WRITE], lotto, sizeof(int) * NUMBERS)) < 0)
            {
                if (errno == EPIPE)
                {
                    close_pipe(toPlayer[i][WRITE]);
                    toPlayer[i][WRITE] = 0;       
                }
                else
                {
                    ERR("write: ");
                }
            }

        }

        printf("Totalizator sportowy: ");
        for (int j = 0; j < NUMBERS; j++)
        {
            printf("%d, ", lotto[j]);
        }
        printf("are today's lucky numbers\n");
    }

    printf("Total bets: %d, Total rewards: %d\n", totalBet, totalReward);

    free(lotto);
    free(bet);
}



