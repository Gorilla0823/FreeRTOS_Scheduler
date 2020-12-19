#define USE_STDPERIPH_DRIVER
#include "stm32f10x.h"
#include "stm32_p103.h"
/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

/* Filesystem includes */
#include "filesystem.h"
#include "fio.h"
#include "romfs.h"

#include "clib.h"
#include "shell.h"
#include "host.h"

/* private macros */
#define Config_log 0
#define Task_delay 100
#define TASK_AMOUNT 4
/* _sromfs symbol can be found in main.ld linker script
 * it contains file system structure of test_romfs directory
 */
extern const unsigned char _sromfs;
xTaskHandle A_Task_Handler=NULL,B_Task_Handler=NULL,C_Task_Handler=NULL,D_Task_Handler=NULL;
//static void setup_hardware();

volatile xSemaphoreHandle serial_tx_wait_sem = NULL;
/* Add for serial input */
volatile xQueueHandle serial_rx_queue = NULL;

/* IRQ handler to handle USART2 interruptss (both transmit and receive
 * interrupts). */
void USART2_IRQHandler()
{
	static signed portBASE_TYPE xHigherPriorityTaskWoken;

	/* If this interrupt is for a transmit... */
	if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET) {
		/* "give" the serial_tx_wait_sem semaphore to notfiy processes
		 * that the buffer has a spot free for the next byte.
		 */
		xSemaphoreGiveFromISR(serial_tx_wait_sem, &xHigherPriorityTaskWoken);

		/* Diables the transmit interrupt. */
		USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
		/* If this interrupt is for a receive... */
	}else if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET){
		char msg = USART_ReceiveData(USART2);

		/* If there is an error when queueing the received byte, freeze! */
		if(!xQueueSendToBackFromISR(serial_rx_queue, &msg, &xHigherPriorityTaskWoken))
			while(1);
	}
	else {
		/* Only transmit and receive interrupts should be enabled.
		 * If this is another type of interrupt, freeze.
		 */
		while(1);
	}

	if (xHigherPriorityTaskWoken) {
		taskYIELD();
	}
}

void send_byte(char ch)
{
	/* Wait until the RS232 port can receive another byte (this semaphore
	 * is "given" by the RS232 port interrupt when the buffer has room for
	 * another byte.
	 */
	while (!xSemaphoreTake(serial_tx_wait_sem, portMAX_DELAY));

	/* Send the byte and enable the transmit interrupt (it is disabled by
	 * the interrupt).
	 */
	USART_SendData(USART2, ch);
	USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

char recv_byte()
{
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
	char msg;
	while(!xQueueReceive(serial_rx_queue, &msg, portMAX_DELAY));
	return msg;
}
/*static void print_state(char *msg,xTaskState state)
{
	printf("%s-",msg);
	switch(state)
	{
		case eRunning:	fio_printf("run");break;
		case eReady:	fio_printf("ready");break;
		case eBlocked:	fio_printf("block");break;
		case eSuspended:fio_printf("suspend");break;
		case eDeleted:	fio_printf("delete");break;
		default:		fio_printf("5");break;
	}
	printf("  ");
}*/

/*static void print_all_state(char *msg)
{
		taskENTER_CRITICAL();
		
		printf("%s:",msg);

		print_state("start",eTaskGetState(StartTask_Handler));
		print_state("A",xTaskGetSchedulerState(A_Task_Handler));
		print_state("B",xTaskGetSchedulerState(B_Task_Handler));
		print_state("C",xTaskGetSchedulerState(C_Task_Handler));
		print_state("D",xTaskGetSchedulerState(D_Task_Handler));
	
		printf("\r\n");
				 
		taskEXIT_CRITICAL();		
}*/
void command_prompt(){
	char buf[128];
	char *argv[20];
    char hint[] = USER_NAME "@" USER_NAME "-STM32:~$ ";
	fio_printf(1, "\n\r%s", hint);
	fio_read(0, buf, 127);
	int n=parse_command(buf, argv);
	/* will return pointer to the command function */
	cmdfunc *fptr=do_command(argv[0]);
	if(fptr!=NULL)
		fptr(n, argv);
	else
		fio_printf(2, "\r\n\"%s\" command not found.\r\n", argv[0]);
	
}
void loggerv2(){
	char *tag = "Name          State   Priority  Stack  Num";
	fio_printf(1,"%s",tag);
}
void Task_D(){
	for(;;){
		vTaskSuspend(C_Task_Handler);
		signed char buf[512];
		fio_printf(1,"Task_D:\r\n");
		loggerv2();
		vTaskList(buf);
		fio_printf(1,"%s",buf);
		command_prompt();
		vTaskDelete(D_Task_Handler);
	}
}
void Task_C(){
	for(;;){
		vTaskSuspend(B_Task_Handler);
		signed char buf[512];
		fio_printf(1,"\n\rTask_C:\r\n");
		loggerv2();
		vTaskList(buf);
		fio_printf(1,"%s",buf);
		command_prompt();
		vTaskResume(D_Task_Handler);
		vTaskDelay(50);
		vTaskDelete(C_Task_Handler);
	}
}
void Task_B(){
	for(;;){
		vTaskSuspend(A_Task_Handler);
		signed char buf[512];
		fio_printf(1,"\n\rTask_B:\r\n");
		loggerv2();
		vTaskList(buf);	
		fio_printf(1,"%s",buf);
		command_prompt();
		vTaskResume(C_Task_Handler);
		vTaskDelay(500);
		vTaskDelete(B_Task_Handler);
	}
}
void Task_A(){
	for(;;){
		vTaskSuspend(C_Task_Handler);
		vTaskSuspend(D_Task_Handler);
		//fio_printf(1,"A is running\n\r");
		signed char buf[512];
		fio_printf(1,"\r\nTask_A:\r\n");
		loggerv2();
		vTaskList(buf);
		fio_printf(1,"%s",buf);
		command_prompt();
		vTaskResume(B_Task_Handler);
		vTaskDelay(500);
		vTaskDelete(A_Task_Handler);
	}
}
/*void FCFS(){
	signed char *pcTaskName = pcTaskGetTaskName( NULL );
	
	taskENTER_CRITICAL();
	
	fio_printf(1,"\r%s is running\n",pcTaskName);
	//vTaskResume(xHandle);
	taskEXIT_CRITICAL();
	fio_printf(1,"\r%s is ending\n",pcTaskName);
	return;
}*/
/*void command_prompt(void *pvParameters)
{	
	//xTaskHandle xHandle = NULL;
	char buf[128];
	char *argv[20];
    char hint[] = USER_NAME "@" USER_NAME "-STM32:~$ ";
	while(1){
		//vTaskSuspendAll();
		FCFS();
		fio_printf(1, "\r%s\n", hint);
		fio_read(0, buf, 127);
	
		int n=parse_command(buf, argv);
*/
		/* will return pointer to the command function */
		/*cmdfunc *fptr=do_command(argv[0]);
		if(fptr!=NULL)
			fptr(n, argv);
		else
			fio_printf(2, "\r\n\"%s\" command not found.\r\n", argv[0]);
	}
	//vTaskDelete(NULL);
}*/

void system_logger(void *pvParameters)
{
    signed char buf[128];
    char output[512] = {0};
    char *tag = "\nName          State   Priority  Stack  Num\n*******************************************\n";
    int handle, error;
    const portTickType xDelay = 100000 / 100;

    handle = host_action(SYS_OPEN, "output/syslog", 4);
    if(handle == -1) {
        fio_printf(1, "Open file error!\n");
        return;
    }

    while(1) {
        memcpy(output, tag, strlen(tag));
        error = host_action(SYS_WRITE, handle, (void *)output, strlen(output));
        if(error != 0) {
            fio_printf(1, "Write file error! Remain %d bytes didn't write in the file.\n\r", error);
            host_action(SYS_CLOSE, handle);
            return;
        }
        vTaskList(buf);
		fio_printf(1,"%s\n",buf);
        memcpy(output, (char *)(buf + 2), strlen((char *)buf) - 2);

        error = host_action(SYS_WRITE, handle, (void *)buf, strlen((char *)buf));
        if(error != 0) {
            fio_printf(1, "Write file error! Remain %d bytes didn't write in the file.\n\r", error);
            host_action(SYS_CLOSE, handle);
            return;
        }

        vTaskDelay(xDelay);
    }
    
    host_action(SYS_CLOSE, handle);
	
}

int main()
{
	
	//char *s[]={"test1","test2","test3","test4"};
	init_rs232();
	enable_rs232_interrupts();
	enable_rs232();
	
	fs_init();
	fio_init();
	
	register_romfs("romfs", &_sromfs);
	/* Create the queue used by the serial task.  Messages for write to
	 * the RS232. */
	vSemaphoreCreateBinary(serial_tx_wait_sem);
	/* Add for serial input 
	 * Reference: www.freertos.org/a00116.html */
	serial_rx_queue = xQueueCreate(1, sizeof(char));

	/* Create tasks to output text read from romfs. */
	xTaskCreate(Task_A,
	            (signed portCHAR *)"Task_A",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY , &A_Task_Handler);
	xTaskCreate(Task_B,
	            (signed portCHAR *)"Task_B",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY , &B_Task_Handler);
	xTaskCreate(Task_C,
	            (signed portCHAR *)"Task_C",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY , &C_Task_Handler);
	xTaskCreate(Task_D,
	            (signed portCHAR *)"Task_D",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY , &D_Task_Handler);
	
#if Config_log
	/* Create a task to record system log. */
	xTaskCreate(system_logger,
	            (signed portCHAR *) "Logger",
	            1024 /* stack size */, NULL, tskIDLE_PRIORITY , NULL);
#endif

	/* Start running the tasks. */
	vTaskStartScheduler();
	//vTaskEndScheduler();
	for(;;);
	return 0;
}

void vApplicationTickHook()
{
}
