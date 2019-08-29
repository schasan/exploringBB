/**
 * @file   gpio_test.c
 * @author Derek Molloy
 * @date   19 April 2015
 * @brief  A kernel module for controlling a GPIO LED/button pair. The device mounts devices via
 * sysfs /sys/class/gpio/gpio115 and gpio49. Therefore, this test LKM circuit assumes that an LED
 * is attached to GPIO 49 which is on P9_23 and the button is attached to GPIO 115 on P9_27. There
 * is no requirement for a custom overlay, as the pins are in their default mux mode states.
 * @see http://www.derekmolloy.ie/
*/

#include "gpio_test.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>                 // Required for the GPIO functions
#include <linux/interrupt.h>            // Required for the IRQ code
#include <linux/timekeeping.h>		// Measure time between interrupts
#include <linux/uaccess.h>		// copy_to_user()
#include <linux/kfifo.h>		// copy_to_user()

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mario Schulz");
MODULE_DESCRIPTION("Interfacing to smartmeter pulses");
MODULE_VERSION("0.8");

static unsigned int gpioPulse = 60;    ///< hard coding the button gpio for this example to P9_27 (GPIO115)
static unsigned int irqNumber;          ///< Used to share the IRQ number within this file
static unsigned int numberPulses = 0;  ///< For information, store the number of button presses
static ktime_t interrupt_time = 0;

typedef struct {
	int pulse_number;
	ktime_t interrupt_time;
	ktime_t interrupt_delta;
} e_fifo;

#define FIFO_SIZE 4096
//#define FIFO_SIZE 32
#define PROC_FIFO "timer-elements-fifo"
static DEFINE_KFIFO(fifo_ring, e_fifo, FIFO_SIZE);


/// Function prototype for the custom IRQ handler function -- see below for the implementation
static irq_handler_t  ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init ebbgpio_init(void){
   int result = 0;
   printk(KERN_INFO "GPIO_TEST: Initializing the GPIO_TEST LKM size struct: %d\n", sizeof(e_fifo));
   // Is the GPIO a valid GPIO number (e.g., the BBB has 4x32 but not all available)
   gpio_request(gpioPulse, "sysfs");       // Set up the gpioPulse
   gpio_direction_input(gpioPulse);        // Set the button GPIO to be an input
   //gpio_set_debounce(gpioPulse, 200);      // Debounce the button with a delay of 200ms
   gpio_export(gpioPulse, false);          // Causes gpio115 to appear in /sys/class/gpio
			                    // the bool argument prevents the direction from being changed
   // Perform a quick test to see that the button is working as expected on LKM load
   printk(KERN_INFO "GPIO_TEST: The button state is currently: %d\n", gpio_get_value(gpioPulse));

   // GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
   irqNumber = gpio_to_irq(gpioPulse);
   printk(KERN_INFO "GPIO_TEST: The button is mapped to IRQ: %d\n", irqNumber);

   // This next call requests an interrupt line
   result = request_irq(irqNumber,             // The interrupt number requested
                        (irq_handler_t) ebbgpio_irq_handler, // The pointer to the handler function below
                        IRQF_TRIGGER_RISING,   // Interrupt on rising edge (button press, not release)
                        "ebb_gpio_handler",    // Used in /proc/interrupts to identify the owner
                        NULL);                 // The *dev_id for shared interrupt lines, NULL is okay

   printk(KERN_INFO "GPIO_TEST: The interrupt request result is: %d\n", result);
   
   result = register_device();
   printk(KERN_INFO "GPIO_TEST: The device register request result is: %d\n", result);
   return result;
}

/** @brief The LKM cleanup function
 *  Similar to the initialization function, it is static. The __exit macro notifies that if this
 *  code is used for a built-in driver (not a LKM) that this function is not required. Used to release the
 *  GPIOs and display cleanup messages.
 */
static void __exit ebbgpio_exit(void){
   printk(KERN_INFO "GPIO_TEST: Interrupts received: %d\n", numberPulses);
   free_irq(irqNumber, NULL);               // Free the IRQ number, no *dev_id required in this case
   gpio_unexport(gpioPulse);               // Unexport the Button GPIO
   gpio_free(gpioPulse);                   // Free the Button GPIO
   unregister_device();
   printk(KERN_INFO "GPIO_TEST: Goodbye from the LKM!\n");
}

/** @brief The GPIO IRQ Handler function
 *  This function is a custom interrupt handler that is attached to the GPIO above. The same interrupt
 *  handler cannot be invoked concurrently as the interrupt line is masked out until the function is complete.
 *  This function is static as it should not be invoked directly from outside of this file.
 *  @param irq    the IRQ number that is associated with the GPIO -- useful for logging.
 *  @param dev_id the *dev_id that is provided -- can be used to identify which device caused the interrupt
 *  Not used in this example as NULL is passed.
 *  @param regs   h/w specific register values -- only really ever used for debugging.
 *  return returns IRQ_HANDLED if successful -- should return IRQ_NONE otherwise.
 */
static irq_handler_t ebbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){
   ktime_t now = ktime_get();
   e_fifo ring_element, ignore_ring_element;
   int element_in_count, element_out_count;

   //printk(KERN_INFO "GPIO_TEST: Interrupt! (button state is %d)\n", gpio_get_value(gpioPulse));
   if (numberPulses)
      printk(KERN_INFO "GPIO_TEST: Interrupt! Pulse number %08d %12lld\n", numberPulses, now-interrupt_time);
   else
      printk(KERN_INFO "GPIO_TEST: Interrupt! Pulse number %08d no delta\n", numberPulses);
   
   ring_element.pulse_number = numberPulses++;    // Global counter, will be outputted when the module is unloaded
   ring_element.interrupt_delta = now-interrupt_time;
   ring_element.interrupt_time = now;
   element_in_count = kfifo_put(&fifo_ring, ring_element);
   printk(KERN_INFO "GPIO_TEST: Elements pushed into fifo: %d\n", element_in_count);
   printk(KERN_INFO "GPIO_TEST: Elements available in fifo: %d\n", kfifo_len(&fifo_ring));
   if (element_in_count == 0) {	// Nothing pushed, fifo is full
      element_out_count = kfifo_out(&fifo_ring, &ignore_ring_element, 1);
      element_in_count = kfifo_put(&fifo_ring, ring_element);
      printk(KERN_INFO "GPIO_TEST: fifo full, deleted %d and pushed %d\n", element_out_count, element_in_count);
      printk(KERN_INFO "GPIO_TEST: Pulled %08d %12lld\n", ignore_ring_element.pulse_number, ignore_ring_element.interrupt_delta);
   }

   interrupt_time = now;

   return (irq_handler_t) IRQ_HANDLED;      // Announce that the IRQ has been handled correctly
}

static const char    g_s_Hello_World_string[] = "Hello world from kernel mode!\n\0";
static const ssize_t g_s_Hello_World_size = sizeof(g_s_Hello_World_string);

/*===============================================================================================*/
static ssize_t device_file_read(struct file *file_ptr, char __user *user_buffer, size_t count, loff_t *position)
{
   size_t ret_count = 0;
   int ret;

   /*
   if (*position >= g_s_Hello_World_size) return 0;
   if (*position + count > g_s_Hello_World_size) count = g_s_Hello_World_size - *position;
   if (copy_to_user(user_buffer, g_s_Hello_World_string + *position, count) != 0) return -EFAULT;

   *position += count;
   */
   if (kfifo_len(&fifo_ring) > 0) {
      ret = kfifo_to_user(&fifo_ring, user_buffer, count, &ret_count);
      *position += ret_count;

      printk(KERN_NOTICE "GPIO_TEST: read offset: %i - read requested: %u - read count: %u - read ret: %d",
		      (int)*position, (unsigned int)count, (unsigned int)ret_count, ret);
   }

   return ret_count;
}

/*===============================================================================================*/
static struct file_operations simple_driver_fops =
{
   .owner   = THIS_MODULE,
   .read    = device_file_read,
};

static int device_file_major_number = 0;
static const char device_name[] = "GPIO_TEST";

/*===============================================================================================*/
int register_device(void)
{
   int result = 0;

   printk(KERN_NOTICE "GPIO_TEST: register_device() is called." );

   result = register_chrdev(0, device_name, &simple_driver_fops);
   if (result < 0) {
     printk(KERN_WARNING "GPIO_TEST:  can\'t register character device with errorcode = %i", result);
     return result;
   }

   device_file_major_number = result;
   printk(KERN_NOTICE "GPIO_TEST: registered character device with major number = %i and minor numbers 0...255", device_file_major_number);

   return 0;
}

/*-----------------------------------------------------------------------------------------------*/
void unregister_device(void)
{
   printk(KERN_NOTICE "GPIO_TEST: unregister_device() is called");
   if (device_file_major_number != 0) unregister_chrdev(device_file_major_number, device_name);
}
//
/// This next calls are  mandatory -- they identify the initialization function
/// and the cleanup function (as above).
module_init(ebbgpio_init);
module_exit(ebbgpio_exit);
