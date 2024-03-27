#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define UNUSED(x) ((void)(x))

#define MAX_QUEUE_NAME 256
#define TASK_QUEUE_NAME "/task_queue_%d"
#define RESULT_QUEUE_NAME "/result_queue_%d_%d"

#define MAX_MSG_SIZE 10  // Max message size
#define MAX_MSGS 10      // Queue size

#define MIN_WORKERS 2
#define MAX_WORKERS 20
#define MIN_TIME 100
#define MAX_TIME 5000
#define TASK_TODO 5

struct task_to_worker
{
    float v1;
    float v2;
};

struct task_from_worker
{
    float result;
    pid_t pid;
};

void create_children(int n, pid_t *pids, mqd_t pout);
void child_work(mqd_t pout, pid_t pid);
void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo);
void mq_handler(int sig, siginfo_t *info, void *p);

void usage(const char *name)
{
    fprintf(stderr, "USAGE: %s N T1 T2\n", name);
    fprintf(stderr, "N: %d <= N <= %d - number of workers\n", MIN_WORKERS, MAX_WORKERS);
    fprintf(stderr, "T1, T2: %d <= T1 < T2 <= %d - time range for spawning new tasks\n", MIN_TIME, MAX_TIME);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    int n;
    int t1;
    int t2;
    srand(time(NULL));
    sethandler(mq_handler, SIGRTMIN);

    if (argc != 4)
    {
        usage(argv[0]);
    }
    n = atoi(argv[1]);
    t1 = atoi(argv[2]);
    t2 = atoi(argv[3]);

    if (n < 2 || n > 20 || t1 < 100 || t2 > 5000 || t1 > t2)
    {
        usage(argv[0]);
    }

    printf("Server is starting...\n");

    mqd_t pout;
    struct mq_attr attr_pout;
    attr_pout.mq_maxmsg = MAX_MSGS;
    attr_pout.mq_msgsize = sizeof(struct task_to_worker);
    int ret, time_sleep;

    char task_name[50];
    snprintf(task_name, 50, "/task_queue_%d", getpid());
    if ((pout = TEMP_FAILURE_RETRY(mq_open(task_name, O_RDWR | O_CREAT, 0600, &attr_pout))) == (mqd_t)-1)
        ERR("mq open out: ");

    pid_t pids[n];
    create_children(n, pids, pout);

    mqd_t pin[n];
    struct mq_attr attr_pin;
    attr_pin.mq_maxmsg = MAX_MSGS;
    attr_pin.mq_msgsize = sizeof(struct task_from_worker);

    char worker_name[n][50];
    for (int i = 0; i < n; i++)
    {
        snprintf(worker_name[i], 50, "/result_queue_%d_%d", getpid(), pids[i]);
        if ((pin[i] = TEMP_FAILURE_RETRY(mq_open(worker_name[i], O_CREAT | O_RDONLY | O_NONBLOCK, 0600, &attr_pin))) ==
            (mqd_t)-1)
            ERR("mq_open");

        static struct sigevent noti;
        noti.sigev_notify = SIGEV_SIGNAL;
        noti.sigev_signo = SIGRTMIN;
        noti.sigev_value.sival_ptr = &pin[i];
        if (mq_notify(pin[i], &noti) < 0)
            ERR("mq_notify");
    }

    mq_close(pout);

    if ((pout = TEMP_FAILURE_RETRY(mq_open(task_name, O_RDWR | O_NONBLOCK, 0600, &attr_pout))) == (mqd_t)-1)
        ERR("mq open out: ");

    for (int i = 0; i < 5 * n; i++)
    {
        struct task_to_worker task;
        task.v1 = (float)rand() / (float)(RAND_MAX / 100.0);
        task.v2 = (float)rand() / (float)(RAND_MAX / 100.0);

        while (task.v1 == task.v2)
        {
            task.v2 = (float)rand() / (float)(RAND_MAX / 100.0);
        }

        errno = 0;
        printf("New task queued: [%f, %f]\n", task.v1, task.v2);
        if ((ret = mq_send(pout, (char *)&task, sizeof(struct task_to_worker), 0)) < 0)
        {
            if (errno == EAGAIN)
            {
                fprintf(stderr, "Parent: Queue is full!\n");
            }
            else
                ERR("mq_send: ");
        }
        time_sleep = t1 + rand() % ((t2 - t1) + 1);
        usleep(time_sleep * 1000);
    }

    for (int i = 0; i < n; i++)
    {
        struct task_to_worker endingTask;
        endingTask.v1 = -1;
        if ((ret = mq_send(pout, (char *)&endingTask, sizeof(struct task_to_worker), 1)) < 0)
            ERR("mq_send: ");
    }

    for (int i = 0; i < n; i++)
    {
        mq_close(pin[i]);
    }
    mq_close(pout);
    if (mq_unlink(task_name))
        ERR("mq_unlink");

    for (int i = 0; i < n; i++)
    {
        wait(NULL);
    }

    printf("All child processes have finished\n");

    return EXIT_SUCCESS;
}

void sethandler(void (*f)(int, siginfo_t *, void *), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_sigaction = f;
    act.sa_flags = SA_SIGINFO;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void create_children(int n, pid_t *pids, mqd_t pout)
{
    int i = 0;
    pid_t pid = getpid();
    while (n-- > 0)
    {
        switch (pids[i++] = fork())
        {
            case 0:
                child_work(pout, pid);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork: ");
        }
    }
}

void child_work(mqd_t pout, pid_t pid)
{
    srand(getpid());
    int time_sleep, ret;
    float result;
    printf("[%d] Worker ready\n", getpid());

    mqd_t pin;
    char worker_name[50];
    snprintf(worker_name, 50, "/result_queue_%d_%d", pid, getpid());

    struct mq_attr attr;
    attr.mq_maxmsg = MAX_MSGS;
    attr.mq_msgsize = sizeof(struct task_from_worker);
    if ((pin = TEMP_FAILURE_RETRY(mq_open(worker_name, O_CREAT | O_WRONLY, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open");

    for (int i = 0; i < 5; i++)
    {
        struct task_to_worker task;
        if (TEMP_FAILURE_RETRY(mq_receive(pout, (char *)&task, sizeof(struct task_to_worker), 0)) < 1)
            ERR("mq_receive: ");

        printf("[%d] Received task [%f, %f]\n", getpid(), task.v1, task.v2);
        time_sleep = 500 + rand() % 1501;
        usleep(time_sleep * 1000);
        result = task.v1 + task.v2;

        struct task_from_worker task_fw;
        task_fw.pid = getpid();
        task_fw.result = result;

        printf("[%d] Result sent: %f\n", getpid(), result);
        if ((ret = mq_send(pin, (char *)&task_fw, sizeof(struct task_from_worker), 0)) < 0)
            ERR("mq_send");
    }

    mq_close(pin);
    if (mq_unlink(worker_name))
        ERR("mq_unlink: ");
    printf("[%d] Exits\n", getpid());
}

void mq_handler(int sig, siginfo_t *info, void *p)
{
    UNUSED(sig);
    UNUSED(p);

    mqd_t pin;
    struct task_from_worker task_fw;
    unsigned msg_prio;

    pin = *((mqd_t *)info->si_value.sival_ptr);

    static struct sigevent not ;
    not .sigev_notify = SIGEV_SIGNAL;
    not .sigev_signo = SIGRTMIN;
    not .sigev_value.sival_ptr = info->si_value.sival_ptr;
    if (mq_notify(pin, &not ) < 0)
        ERR("mq_notify");

    for (;;)
    {
        errno = 0;
        if (mq_receive(pin, (char *)&task_fw, sizeof(struct task_from_worker), &msg_prio) < 1)
        {
            if (errno == EAGAIN)
                break;
            else
                ERR("mq_receive");
        }
        printf("Result from worker [%d]: %f\n", task_fw.pid, task_fw.result);
    }
}
