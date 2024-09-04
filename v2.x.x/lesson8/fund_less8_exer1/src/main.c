//#include <zephyr.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/rand32.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel/thread_stack.h>

#define SLEEP_TIME_MS   100
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define PRODUCER_STACKSIZE       2048
#define CONSUMER_STACKSIZE       2048
#define PRODUCER_PRIORITY        5 
#define CONSUMER_PRIORITY        4
#define MAIN_MUTEX_PRIORITY     6

struct k_sem instance_monitor_sem;
struct k_mutex my_mutex;

k_tid_t producer_thread_id;
k_tid_t consumer_1_thread_id;
k_tid_t consumer_2_thread_id;
k_tid_t main_mutex_thread_id;

K_KERNEL_STACK_DEFINE(producer_thread_stack, PRODUCER_STACKSIZE);
K_KERNEL_STACK_DEFINE(consumer_1_thread_stack, CONSUMER_STACKSIZE);
K_KERNEL_STACK_DEFINE(consumer_2_thread_stack, CONSUMER_STACKSIZE);
K_KERNEL_STACK_DEFINE(main_mutex_thread_stack, CONSUMER_STACKSIZE);

volatile uint32_t available_instance_count = 10;

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
int ret;

// Function for getting access to the resource
void get_access(const char *consumer_name)
{
    // Attempt to obtain mutex to prevent deadlock
    if (k_mutex_lock(&my_mutex, K_NO_WAIT) != 0) {
        printk("Mutex not available for %s\n", consumer_name);
        return;
    }

    /* Get semaphore before access to the resource */
    if (k_sem_take(&instance_monitor_sem, K_MSEC(50)) != 0) {
        printk("Semaphore not available for %s\n", consumer_name);
        k_mutex_unlock(&my_mutex); // Release mutex to prevent deadlock
        return;
    }

    /* Print the consumer name along with the resource status */
    printk("%s: Resource taken and available_instance_count = %d\n",
           consumer_name, k_sem_count_get(&instance_monitor_sem));

    k_mutex_unlock(&my_mutex); // Release mutex
}

// Function for releasing access to the resource
void release_access(const char *consumer_name)
{
    // Attempt to obtain mutex to prevent deadlock
    if (k_mutex_lock(&my_mutex, K_NO_WAIT) != 0) {
        printk("Mutex not available for %s\n", consumer_name);
        return;
    }

    /* Print the consumer name along with the resource status */
    printk("%s: Resource given and available_instance_count = %d\n",
           consumer_name, k_sem_count_get(&instance_monitor_sem));

    /* Give semaphore after finishing access to the resource */
    k_sem_give(&instance_monitor_sem);
    k_mutex_unlock(&my_mutex);
}

/* Producer thread releasing access to instance */
void producer(void)
{
    printk("Producer thread started\n");
    while (1) {
        // Check if resource is available
        if (k_sem_count_get(&instance_monitor_sem) > 0) {
            release_access("Producer");
            ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
            ret = gpio_pin_toggle_dt(&led1);
            // Assume the resource instance access is released at this point
            k_msleep(1500 + sys_rand32_get() % 10);
        } 
    }   
}


/* STEP 5 - Consumer thread obtaining access to instance */
void consumer1_thread(const char *consumer_name)
{
    printk("%s thread started\n", consumer_name);

    while (1) {
        get_access(consumer_name);
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        ret = gpio_pin_toggle_dt(&led);
        // Assume the resource instance access is released at this point
        k_msleep(1000 + sys_rand32_get() % 10);
    }
}

void consumer2_thread(const char *consumer_name)
{
    printk("%s thread started\n", consumer_name);

    while (1) {
        get_access(consumer_name);
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        ret = gpio_pin_toggle_dt(&led);
        // Assume the resource instance access is released at this point
        k_msleep(1000 + sys_rand32_get() % 10);
    }
}

// Thread to acquire the mutex in main
void main_mutex_thread(k_tid_t unused)
{
    printk("Main mutex thread started\n");
    while (1) {
        if (k_mutex_lock(&my_mutex, K_FOREVER) == 0) {
            printk("Main took mutex\n");
            k_msleep(100);
            k_mutex_unlock(&my_mutex);
            break;
        } else {
            k_msleep(100);
        }
    }
}

void create_threads(void)
{
    k_sem_init(&instance_monitor_sem, 10, 10);
    k_mutex_init(&my_mutex);

    struct k_thread producer_thread_data;
    producer_thread_id = k_thread_create(&producer_thread_data, producer_thread_stack,
                                    PRODUCER_STACKSIZE, (k_thread_entry_t)producer, NULL, NULL, NULL,
                                    PRODUCER_PRIORITY, 0, K_NO_WAIT);

    struct k_thread consumer_1_thread_data;
    consumer_1_thread_id = k_thread_create(&consumer_1_thread_data, consumer_1_thread_stack,
                                    CONSUMER_STACKSIZE, (k_thread_entry_t)consumer1_thread, "Consumer_1", NULL, NULL,
                                    CONSUMER_PRIORITY, 0, K_SECONDS(3));

    struct k_thread consumer_2_thread_data;
    consumer_2_thread_id = k_thread_create(&consumer_2_thread_data, consumer_2_thread_stack,
                                    CONSUMER_STACKSIZE, (k_thread_entry_t)consumer2_thread, "Consumer_2", NULL, NULL,
                                    CONSUMER_PRIORITY, 0, K_SECONDS(3));
    
    struct k_thread main_mutex_thread_data;
    main_mutex_thread_id = k_thread_create(&main_mutex_thread_data, main_mutex_thread_stack,
                                    CONSUMER_STACKSIZE, (k_thread_entry_t)main_mutex_thread, NULL, NULL, NULL,
                                    MAIN_MUTEX_PRIORITY, 0, K_NO_WAIT);

    k_thread_start(producer_thread_id);
    k_thread_start(consumer_1_thread_id);
    k_thread_start(consumer_2_thread_id);
    k_thread_start(main_mutex_thread_id);
    printk("Threads created\n");
}

void cleanup_threads(void)
{
    printk("i am here\n");
    k_thread_suspend(producer_thread_id);
    k_thread_suspend(consumer_1_thread_id);
    k_thread_suspend(consumer_2_thread_id);

    k_sem_reset(&instance_monitor_sem);
    k_mutex_init(&my_mutex);

    printk("Threads and resources cleaned up\n");
}

void main(void)
{
    create_threads();
    //k_busy_wait(K_SECONDS(5));
    k_sleep(K_FOREVER);
    k_mutex_init(&my_mutex);
    cleanup_threads();

    while (1) {
        printk("Main loop\n");
        k_sleep(K_FOREVER);
    }
}
