#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<semaphore.h>
#include<pthread.h>
#include<unistd.h>

#define MAXTABLES 20
#define MAXCOOKS 20
#define MAXDINERS 20
#define MAXBURGERS 20
#define MAXFRIES 20

typedef enum T_FOOD { BURGER = 1, FRIES, COKE } T_FOOD;

int
i = 0,
j = 0,
k = 0,
no_of_diners,
no_of_tables,
no_of_cooks,

burger_machine_last_used,
fries_machine_last_used,
coke_machine_last_used,

table[MAXTABLES],
table_freed_at[MAXTABLES],
cook_freed_at[MAXCOOKS],
cook_starts_prep_at[MAXCOOKS],

dinersRemaining,
tablesAvailable,
cooksAvailable;

sem_t

sem_wake_diner[MAXDINERS],
               sem_wake_cook[MAXCOOKS],
               sem_table_list[MAXTABLES],
               sem_cook_list[MAXCOOKS],
               sem_wake_waiting_diner[MAXDINERS],
               sem_wake_waiting_diner_for_cook[MAXDINERS],
               sem_wake_waiting_cook[MAXCOOKS],
               sem_burger,
               sem_fries,
               sem_coke;

pthread_mutex_t
mutex_table,
mutex_cook,
mutex_diner,
mutex_burger,
mutex_fries,
mutex_coke;

pthread_t c_id[MAXCOOKS], d_id[MAXTABLES];

struct cook
{
    int cook_no;
    int cook_avail;
    int diner_assigned;
    T_FOOD isWaiting;

} cooks[MAXCOOKS];

struct diner
{
    int isWaiting;
    int isWaitingForCook;
    int diner_no;
    int in_time;
    int cook_assigned;
    int cook_assigned_time;
    int table_assigned;
    int table_assigned_time;
    int food_served_time;
    int out_time;
    struct order
    {
        int burgers;
        int fries;
        int coke;
    } foodorder;
    struct machine_use
    {
        int burger_assign_time[MAXBURGERS];
        int fries_assign_time[MAXFRIES];
        int coke_assign_time;
    } machine;
} diners[MAXDINERS];

void wakeup_waiting_diner(int);
void input_read(char *filename);
void wakeup_cook(int);
void wakeup_waiting_cook();
void process_order(int, int);
void *cook(void *param);
void *diner(void *param);
int wakeup_waiting_diner_for_cook(int);
int max(int, int);

int main(int argc, char *argv[])
{
    input_read(argv[1]);
    int diner_id[MAXDINERS], cook_id[MAXCOOKS];
    int i, j, k;

    /*intializing the semaphores and the mutexs*/
    sem_init(&sem_burger, 0, 1);
    sem_init(&sem_fries, 0, 1);
    sem_init(&sem_coke, 0, 1);

    pthread_mutex_init(&mutex_table, NULL);
    pthread_mutex_init(&mutex_cook, NULL);
    pthread_mutex_init(&mutex_burger, NULL);
    pthread_mutex_init(&mutex_fries, NULL);
    pthread_mutex_init(&mutex_coke, NULL);
    pthread_mutex_init(&mutex_diner, NULL);

    for (i = 0; i < no_of_cooks; i++)
    {
        // all the cooks are initially sleeping
        //  they need to be woke up by some customer, who gets the table
        sem_init(&sem_wake_cook[i], 0, 0);

        // initialize one instance of every cook
        sem_init(&sem_cook_list[i], 0, 1);
    }

    for (i = 0; i < no_of_diners; i++)
    {
        diners[i].isWaiting = 1;
        diners[i].isWaitingForCook = 1;

        // wake diner waiting for the order, when the order is prepared
        sem_init(&sem_wake_diner[i], 0, 0);

        // wake diner waiting for a table, when some table gets free
        sem_init(&sem_wake_waiting_diner[i], 0, 0);

        // wake diner waiting for a cook, when some cooks gets free, and the customer has already occupied a table
        sem_init(&sem_wake_waiting_diner_for_cook[i], 0, 0);
    }

    for (i = 0; i < no_of_tables; i++)
    {
        // initialize one instance of every table
        sem_init(&sem_table_list[i], 0, 1);
    }

    for (i = 0; i < no_of_cooks; i++) // creating threads for each cook
    {
        cook_id[i] = i;
        pthread_create(&c_id[i], NULL, &cook, &cook_id[i]);
    }
    j = 0;

    for (k = 0; k < 120; k++) // timer loop for 120 mins
    {
        if (diners[j].in_time == k)
        {
            // creating thread for each diner at moment k
            diner_id[j] = j;
            pthread_create(&d_id[j], NULL, &diner, &diner_id[j]);
            j++;
        }
        sleep(1);
    } //end for loop of timer

    for (i = 0; i < no_of_diners; i++)
    {
        // wait/halt the main thread till the child/diner threads finish
        pthread_join(d_id[i], NULL);

        // close the main thread, when diners remaining to be serviced are zero
        if (dinersRemaining == 0)
        {
            printf("\n All diners have exited.");
            exit(0);
        }
    }
} // end main

void *cook(void *param)
{
    int diner_id;
    int cook_id = *((int *)param);      // Extract the thread ID

    while (1)
    {
        // sleeping till some customer wakes me up
        sem_wait(&sem_wake_cook[cook_id]);
        diner_id = cooks[cook_id].diner_assigned;
        process_order(cook_id, diner_id);
        // wake the customer up, when order is ready
        sem_post(&sem_wake_diner[diner_id]);
    }
}

/*
    function to wake a cook up, who is waiting for machine(burger, fries or coke)
    the machine he's waiting for is indicated by the function parameter
*/
void wakeup_waiting_cook(T_FOOD type)
{
    int i;

    for (i = 0; i < no_of_cooks; ++i)
    {
        if (cooks[i].isWaiting)
        {
            // check necessary to ensure, a diner is woke up just for the machine he's waiting
            // e.g : don't wake a cook, when burger machine gets free, if he's waiting for fries machine
            if (cooks[i].isWaiting == type)
            {
                cooks[i].isWaiting = 0;
                sem_post(&sem_wake_waiting_cook[i]);
            }

            break;
        }
    }
}

void process_order(int cook_id, int diner_id)
{
    int timer = cook_starts_prep_at[cook_id];
    int order_completed = 0; int b = 0, f = 0, c = 0;

    if (!burger_machine_last_used)
    {
        burger_machine_last_used = timer;
    }
    while (order_completed == 0)
    {

        burger_machine_last_used = max(burger_machine_last_used, timer);
        if (diners[diner_id].foodorder.burgers != 0)
        {

            if (sem_wait(&sem_burger) != 0)
            {
                cooks[cook_id].isWaiting = BURGER;
                sem_wait(&sem_wake_waiting_cook[cook_id]);
            }

            pthread_mutex_lock(&mutex_burger);

            while (diners[diner_id].foodorder.burgers != 0)
            {

                diners[diner_id].foodorder.burgers--;
                b++;
                diners[diner_id].machine.burger_assign_time[b - 1] = burger_machine_last_used;
                printf("COOKED\tCook %d\tBurger %d Diner %d Time %d \n", cook_id + 1, b, diner_id + 1, diners[diner_id].machine.burger_assign_time[b - 1]);
                burger_machine_last_used += 5;
                sleep(5);
            }

            pthread_mutex_unlock(&mutex_burger);
            sem_post(&sem_burger);
            wakeup_waiting_cook(BURGER);
        }

        fries_machine_last_used = max(burger_machine_last_used, timer);
        if (diners[diner_id].foodorder.fries != 0)
        {
            if (sem_wait(&sem_fries) != 0)
            {
                cooks[cook_id].isWaiting = FRIES;
                sem_wait(&sem_wake_waiting_cook[cook_id]);
            }
            pthread_mutex_lock(&mutex_fries);
            while (diners[diner_id].foodorder.fries != 0)
            {

                diners[diner_id].foodorder.fries--;
                f++;
                diners[diner_id].machine.fries_assign_time[f - 1] = fries_machine_last_used;
                printf("COOKED\tCook %d\tFries %d\tDiner %d\tTime %d \n", cook_id + 1, f, diner_id + 1, diners[diner_id].machine.fries_assign_time[f - 1]);
                sleep(3);
                fries_machine_last_used += 3;
            }
            pthread_mutex_unlock(&mutex_fries);
            sem_post(&sem_fries);
            wakeup_waiting_cook(FRIES);
        }

        coke_machine_last_used = max(fries_machine_last_used, timer);
        if (diners[diner_id].foodorder.coke != 0)
        {

            if (sem_wait(&sem_coke) != 0)
            {
                cooks[cook_id].isWaiting = FRIES;
                sem_wait(&sem_wake_waiting_cook[cook_id]);
            }
            pthread_mutex_lock(&mutex_coke);
            while (diners[diner_id].foodorder.coke != 0)
            {
                diners[diner_id].foodorder.coke --;
                c++;
                diners[diner_id].machine.coke_assign_time = coke_machine_last_used;
                printf("COOKED\tCook %d\tCoke %d\tDiner %d\tTime %d \n", cook_id + 1, c, diner_id + 1, diners[diner_id].machine.coke_assign_time);
                sleep(1);
                coke_machine_last_used += 1;
            }
            pthread_mutex_unlock(&mutex_coke);
            sem_post(&sem_coke);
            wakeup_waiting_cook(COKE);
        }

        if ((diners[diner_id].foodorder.burgers == 0) && (diners[diner_id].foodorder.fries == 0) && (diners[diner_id].foodorder.coke == 0))
        {
            order_completed = 1;
            diners[diner_id].food_served_time = coke_machine_last_used;
            cook_freed_at[cook_id] = coke_machine_last_used;
        }
    }
}

void getCook(int diner_id, int wait_for_cook)
{
    int i;

    for (i = 0; i < no_of_cooks; i++) //checks if there are any free cooks
    {
        if (cooks[i].cook_avail == 1)
        {
            cooks[i].cook_avail = 0;
            cooksAvailable--;
            sem_wait(&sem_cook_list[i]);

            diners[diner_id].cook_assigned = i; // assign the free available cook
            cooks[i].diner_assigned = diner_id; //cook gets a diner
            if (wait_for_cook == 0)
                diners[diner_id].cook_assigned_time = diners[diner_id].table_assigned_time;
            else
            {
                wait_for_cook = 0;
                diners[diner_id].cook_assigned_time = max(cook_freed_at[i], diners[diner_id].table_assigned_time); //have to assign time when cook leaves
            }

            cook_starts_prep_at[i] = diners[diner_id].cook_assigned_time;
            printf("ORDERED\tDiner %d\tCook %d\tTime %d \n", diner_id + 1, diners[diner_id].cook_assigned + 1, diners[diner_id].cook_assigned_time);
            sem_post(&sem_wake_cook[i]);
            break;
        }
    }
}

void getTable(int diner_id, int wait_for_table)
{
    for (i = 0; i < no_of_tables; i++) //checks if there are any free tables
    {
        if (table[i] == 0)
        {
            table[i] = 1  ;
            tablesAvailable--;
            diners[diner_id].table_assigned = i;   //acquires free table
            // lock the table, no one can take it
            sem_wait(&sem_table_list[i]);

            diners[diner_id].table_assigned_time = max(diners[diner_id].in_time, table_freed_at[i]); //when diner has been seated after waiting when table was last left
            printf("SEATED\tDiner %d\tTable %d\tTime %d\n", diner_id + 1, diners[diner_id].table_assigned + 1, diners[diner_id].table_assigned_time );
            break;
        }
    }
}

void *diner(void *param)
{
    int diner_id = *((int *)param);

    diners[diner_id].diner_no = diner_id;
    printf("ENTERED\tDiner %d\tTime %d\n", diner_id+1, diners[diner_id].in_time );
    pthread_mutex_lock(&mutex_table);
    if (!tablesAvailable)
    {
        pthread_mutex_unlock(&mutex_table);
        sem_wait(&sem_wake_waiting_diner[diner_id]);
    }

    diners[diner_id].isWaiting = 0;
    getTable(diner_id, 0);

    pthread_mutex_lock(&mutex_cook);
    pthread_mutex_unlock(&mutex_table);

    if (!cooksAvailable)
    {
        pthread_mutex_unlock(&mutex_cook);
        sem_wait(&sem_wake_waiting_diner_for_cook[diner_id]);
    }

    diners[diner_id].isWaitingForCook = 0;
    getCook(diner_id, 0);
    pthread_mutex_unlock(&mutex_cook);

    sem_wait(&sem_wake_diner[diner_id]);
    printf("SERVED\tDiner %d\tCook %d\tTime %d\n", diner_id + 1, diners[diner_id].cook_assigned + 1, diners[diner_id].food_served_time);

    pthread_mutex_lock(&mutex_cook);
    cooks[diners[diner_id].cook_assigned].cook_avail = 1;
    cooksAvailable++;
    sem_post(&sem_cook_list[diners[diner_id].cook_assigned]);
    wakeup_waiting_diner_for_cook(diner_id);

    diners[diner_id].out_time = diners[diner_id].food_served_time + 30;
    sleep(30);

    pthread_mutex_lock(&mutex_table);
    table_freed_at[diners[diner_id].table_assigned] = diners[diner_id].out_time;
    tablesAvailable++;
    table[diners[diner_id].table_assigned] = 0; // freeing that table before exiting
    sem_post(&sem_table_list[diners[diner_id].table_assigned]);
    wakeup_waiting_diner(diner_id);

    printf("LEFT\tDiner %d\tTable %d\tTime %d\n", diner_id + 1, diners[diner_id].table_assigned + 1, diners[diner_id].out_time);
    pthread_mutex_lock(&mutex_diner);
    dinersRemaining--;
    pthread_mutex_unlock(&mutex_diner);
    pthread_exit(NULL);
} // end diner

void wakeup_waiting_diner(int diner_id)
{
    int i;

    for (i = 0; i < no_of_diners; ++i)
    {
        if (diners[i].isWaiting == 1)
        {
            sem_post(&sem_wake_waiting_diner[i]);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_table);
}

int wakeup_waiting_diner_for_cook(int diner_id)
{
    int i, status = 1;

    for (i = 0; i < no_of_diners; ++i)
    {
        if (diners[i].isWaitingForCook == 1)
        {
            sem_post(&sem_wake_waiting_diner_for_cook[i]);
            break;
        }
    }

    pthread_mutex_unlock(&mutex_cook);
    return status;
}

void input_read(char *filename)
{
    int i, j, k;
    FILE *fp;
    char diner[3], table[3], cook[3], diner_info[80];
    char in_time[4], no_burgers[2], no_fries[2], no_coke[2];

    fp = fopen(filename, "r");

    fgets(diner, 1024, fp);
    no_of_diners = atoi(diner);
    dinersRemaining = no_of_diners;
    printf("--------------------------------------------------------\n");
    printf("Diners\t%d \n", no_of_diners);
    fgets(table, 1024, fp);
    no_of_tables = atoi(table);
    tablesAvailable = no_of_tables;
    printf("Tables\t%d \n", no_of_tables);
    fgets(cook, 1024, fp);
    no_of_cooks = atoi(cook);
    cooksAvailable = no_of_cooks;
    printf("Cooks\t%d \n", no_of_cooks);
    printf("--------------------------------------------------------\n");
    for (i = 0; i < no_of_diners; i++)
    {
        diners[i].diner_no = i;
        diners[i].cook_assigned = 0;
        diners[i].table_assigned = 0;
    }

    for (i = 0; i < no_of_cooks; i++)
    {
        cooks[i].cook_no = i;
        cooks[i].cook_avail = 1;
        cooks[i].diner_assigned = 0;
    }

    while (fgets(diner_info, 1024, fp) != NULL)
    {
        strcpy(in_time, "\0");
        strcpy(no_burgers, "\0");
        strcpy(no_fries, "\0");
        strcpy(no_coke, "\0");
        static int i = -1;
        i++;
        j = 0;
        k = 0;
        while (diner_info[j] != ' ')
        {
            in_time[k] = diner_info[j];
            j++; k++;
        }
        in_time[k] = '\0';
        diners[i].in_time = atoi(in_time);

        while (diner_info[j] == ' ')
            j++;
        k = 0;
        while (diner_info[j] != ' ')
        {
            no_burgers[k] = diner_info[j];
            j++; k++;
        }
        no_burgers[k] = '\0';
        diners[i].foodorder.burgers = atoi(no_burgers);

        while (diner_info[j] == ' ')
            j++;
        k = 0;
        while (diner_info[j] != ' ')
        {
            no_fries[k] = diner_info[j];
            j++; k++;
        }
        no_fries[k] = '\0';
        diners[i].foodorder.fries = atoi(no_fries);

        while (diner_info[j] == ' ')
            j++;
        k = 0;
        while (diner_info[j] != '\0')
        {
            no_coke[k] = diner_info[j];
            j++; k++;
        }
        no_coke[k] = '\0';
        diners[i].foodorder.coke = atoi(no_coke);
        strcpy(diner_info, "\0");
    }
    fclose(fp);
    for (i = 0; i < diners[i].foodorder.burgers; i++)
    {
        diners[i].machine.burger_assign_time[i] = 0;
    }
    for (i = 0; i < diners[i].foodorder.fries; i++)
    {
        diners[i].machine.fries_assign_time[i] = 0;
    }
    diners[i].machine.coke_assign_time = 0;
}



int max(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}
