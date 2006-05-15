
#include "uart.h"

#define UART0_TX_INT_MODE 1
#define UART0_RX_INT_MODE 1

#if defined(UART0_TX_INT_MODE) || defined(UART0_RX_INT_MODE) || \
    defined(UART1_TX_INT_MODE) || defined(UART1_RX_INT_MODE)
#include "armVIC.h"
#endif

#if defined(UART0_TX_INT_MODE) || defined(UART0_RX_INT_MODE)
void uart0_ISR(void) __attribute__((naked));
#endif


#ifdef UART0_RX_INT_MODE
uint8_t  uart0_rx_buffer[UART0_RX_BUFFER_SIZE];
uint16_t uart0_rx_insert_idx, uart0_rx_extract_idx;
#endif

#ifdef UART0_TX_INT_MODE
uint8_t  uart0_tx_buffer[UART0_TX_BUFFER_SIZE];
uint16_t uart0_tx_insert_idx, uart0_tx_extract_idx;
uint8_t  uart0_tx_running;
#endif

static void uart0_init_param( uint16_t baud, uint8_t mode, uint8_t fmode);

void uart0_init_tx( void ) {
  /**  uart0_init_param(UART_BAUD(38400), UART_8N1, UART_FIFO_8); **/
  uart0_init_param(UART0_BAUD, UART_8N1, UART_FIFO_8);
}

void uart0_init_rx( void ) {}

static void uart0_init_param( uint16_t baud, uint8_t mode, uint8_t fmode) {
  // set port pins for UART0
  PINSEL0 = (PINSEL0 & ~U0_PINMASK) | U0_PINSEL;

  U0IER = 0x00;                         // disable all interrupts
  U0IIR;                                // clear interrupt ID
  U0RBR;                                // clear receive register
  U0LSR;                                // clear line status register
  
  // set the baudrate
  U0LCR = ULCR_DLAB_ENABLE;             // select divisor latches 
  U0DLL = (uint8_t)baud;                // set for baud low byte
  U0DLM = (uint8_t)(baud >> 8);         // set for baud high byte
  
  // set the number of characters and other
  // user specified operating parameters
  U0LCR = (mode & ~ULCR_DLAB_ENABLE);
  U0FCR = fmode;
#if defined(UART0_TX_INT_MODE) || defined(UART0_RX_INT_MODE)
  // initialize the interrupt vector
  VICIntSelect &= ~VIC_BIT(VIC_UART0);  // UART0 selected as IRQ
  VICIntEnable = VIC_BIT(VIC_UART0);    // UART0 interrupt enabled
  VICVectCntl5 = VIC_ENABLE | VIC_UART0;
  VICVectAddr5 = (uint32_t)uart0_ISR;    // address of the ISR

#ifdef UART0_TX_INT_MODE
  // initialize the transmit data queue
  uart0_tx_extract_idx = 0;
  uart0_tx_insert_idx = 0;
  uart0_tx_running = 0;
#endif

#ifdef UART0_RX_INT_MODE
  // initialize the receive data queue
  uart0_rx_extract_idx = 0;
  uart0_rx_insert_idx = 0;

  // enable receiver interrupts
  U0IER = UIER_ERBFI;
#endif
#endif
}

bool_t uart0_check_free_space( uint8_t len) {
  int16_t space;
  if ((space = (uart0_tx_extract_idx - uart0_tx_insert_idx)) <= 0)
    space += UART0_TX_BUFFER_SIZE;
  
  return (uint16_t)(space - 1) >= len;
}

void uart0_transmit( unsigned char data ) {
#ifdef UART0_TX_INT_MODE
  uint16_t temp;
  unsigned cpsr;

  temp = (uart0_tx_insert_idx + 1) % UART0_TX_BUFFER_SIZE;

  if (temp == uart0_tx_extract_idx)
    //    return -1;                          // no room
    return;                          // no room

  cpsr = disableIRQ();                  // disable global interrupts
  U0IER &= ~UIER_ETBEI;                 // disable TX interrupts
  restoreIRQ(cpsr);                     // restore global interrupts

  // check if in process of sending data
  if (uart0_tx_running)
    {
    // add to queue
    uart0_tx_buffer[uart0_tx_insert_idx] = (uint8_t)data;
    uart0_tx_insert_idx = temp;
    }
  else
    {
    // set running flag and write to output register
    uart0_tx_running = 1;
    U0THR = (uint8_t)data;
    }

  cpsr = disableIRQ();                  // disable global interrupts
  U0IER |= UIER_ETBEI;                  // enable TX interrupts
  restoreIRQ(cpsr);                     // restore global interrupts
#else
  while (!(U0LSR & ULSR_THRE))          // wait for TX buffer to empty
    continue;                           // also either WDOG() or swap()

  U0THR = (uint8_t)ch;
#endif
  //  return (uint8_t)ch;
}


void uart0_ISR(void)
{
  uint8_t iid;

  // perform proper ISR entry so thumb-interwork works properly
  ISR_ENTRY();

  // loop until not more interrupt sources
  while (((iid = U0IIR) & UIIR_NO_INT) == 0)
    {
    // identify & process the highest priority interrupt
    switch (iid & UIIR_ID_MASK)
      {
      case UIIR_RLS_INT:                // Receive Line Status
        U0LSR;                          // read LSR to clear
        break;

#ifdef UART0_RX_INT_MODE
      case UIIR_CTI_INT:                // Character Timeout Indicator
      case UIIR_RDA_INT:                // Receive Data Available
        do
          {
          uint16_t temp;

          // calc next insert index & store character
          temp = (uart0_rx_insert_idx + 1) % UART0_RX_BUFFER_SIZE;
          uart0_rx_buffer[uart0_rx_insert_idx] = U0RBR;

          // check for more room in queue
          if (temp != uart0_rx_extract_idx)
            uart0_rx_insert_idx = temp; // update insert index
          }
        while (U0LSR & ULSR_RDR);

        break;
#endif

#ifdef UART0_TX_INT_MODE
      case UIIR_THRE_INT:               // Transmit Holding Register Empty
        while (U0LSR & ULSR_THRE)
          {
          // check if more data to send
          if (uart0_tx_insert_idx != uart0_tx_extract_idx)
            {
            U0THR = uart0_tx_buffer[uart0_tx_extract_idx++];
            uart0_tx_extract_idx %= UART0_TX_BUFFER_SIZE;
            }
          else
            {
            // no
            uart0_tx_running = 0;       // clear running flag
            break;
            }
          }

        break;
#endif // UART0_TX_INT_MODE

      default:                          // Unknown
        U0LSR;
        U0RBR;
        break;
      }
    }

  VICVectAddr = 0x00000000;             // clear this interrupt from the VIC
  ISR_EXIT();                           // recover registers and return
}

/*
 *
 * UART1 handling functions - those are pale copies of UART0 ones
 * We should probably find a better way to make the code configurable
 * for both uarts
 *
 */
#define UART1_TX_INT_MODE 1
#define UART1_RX_INT_MODE 1

#if defined(UART1_TX_INT_MODE) || defined(UART1_RX_INT_MODE)
void uart1_ISR(void) __attribute__((naked));
#endif

#ifdef UART1_RX_INT_MODE
uint8_t  uart1_rx_buffer[UART1_RX_BUFFER_SIZE];
uint16_t uart1_rx_insert_idx, uart1_rx_extract_idx;
#endif

#ifdef UART1_TX_INT_MODE
uint8_t  uart1_tx_buffer[UART1_TX_BUFFER_SIZE];
uint16_t uart1_tx_insert_idx, uart1_tx_extract_idx;
uint8_t  uart1_tx_running;
#endif

static void uart1_init_param( uint16_t baud, uint8_t mode, uint8_t fmode);

void uart1_init_tx( void ) {
  uart1_init_param(UART1_BAUD, UART_8N1, UART_FIFO_8);
}

void uart1_init_rx( void ) {}

static void uart1_init_param( uint16_t baud, uint8_t mode, uint8_t fmode) {
  // set port pins for UART1
  PINSEL0 = (PINSEL0 & ~U1_PINMASK) | U1_PINSEL;

  U1IER = 0x00;                         // disable all interrupts
  U1IIR;                                // clear interrupt ID
  U1RBR;                                // clear receive register
  U1LSR;                                // clear line status register
  
  // set the baudrate
  U1LCR = ULCR_DLAB_ENABLE;             // select divisor latches 
  U1DLL = (uint8_t)baud;                // set for baud low byte
  U1DLM = (uint8_t)(baud >> 8);         // set for baud high byte
  
  // set the number of characters and other
  // user specified operating parameters
  U1LCR = (mode & ~ULCR_DLAB_ENABLE);
  U1FCR = fmode;
#if defined(UART1_TX_INT_MODE) || defined(UART1_RX_INT_MODE)
  // initialize the interrupt vector
  VICIntSelect &= ~VIC_BIT(VIC_UART1);  // UART0 selected as IRQ
  VICIntEnable = VIC_BIT(VIC_UART1);    // UART0 interrupt enabled
  VICVectCntl6 = VIC_ENABLE | VIC_UART1;
  VICVectAddr6 = (uint32_t)uart1_ISR;    // address of the ISR

#ifdef UART1_TX_INT_MODE
  // initialize the transmit data queue
  uart1_tx_extract_idx = 0;
  uart1_tx_insert_idx = 0;
  uart1_tx_running = 0;
#endif

#ifdef UART1_RX_INT_MODE
  // initialize the receive data queue
  uart1_rx_extract_idx = 0;
  uart1_rx_insert_idx = 0;

  // enable receiver interrupts
  U1IER = UIER_ERBFI;
#endif
#endif
}

bool_t uart1_check_free_space( uint8_t len) {
  int16_t space;
  if ((space = (uart1_tx_extract_idx - uart1_tx_insert_idx)) <= 0)
    space += UART1_TX_BUFFER_SIZE;
  
  return (uint16_t)(space - 1) >= len;
}

void uart1_transmit( unsigned char data ) {
#ifdef UART1_TX_INT_MODE
  uint16_t temp;
  unsigned cpsr;

  temp = (uart1_tx_insert_idx + 1) % UART1_TX_BUFFER_SIZE;

  if (temp == uart1_tx_extract_idx)
    //    return -1;                          // no room
    return;                          // no room

  cpsr = disableIRQ();                  // disable global interrupts
  U1IER &= ~UIER_ETBEI;                 // disable TX interrupts
  restoreIRQ(cpsr);                     // restore global interrupts

  // check if in process of sending data
  if (uart1_tx_running)
    {
    // add to queue
    uart1_tx_buffer[uart1_tx_insert_idx] = (uint8_t)data;
    uart1_tx_insert_idx = temp;
    }
  else
    {
    // set running flag and write to output register
    uart1_tx_running = 1;
    U1THR = (uint8_t)data;
    }

  cpsr = disableIRQ();                  // disable global interrupts
  U1IER |= UIER_ETBEI;                  // enable TX interrupts
  restoreIRQ(cpsr);                     // restore global interrupts
#else
  while (!(U1LSR & ULSR_THRE))          // wait for TX buffer to empty
    continue;                           // also either WDOG() or swap()

  U1THR = (uint8_t)ch;
#endif
  //  return (uint8_t)ch;
}


void uart1_ISR(void)
{
  uint8_t iid;

  // perform proper ISR entry so thumb-interwork works properly
  ISR_ENTRY();

  // loop until not more interrupt sources
  while (((iid = U1IIR) & UIIR_NO_INT) == 0)
    {
    // identify & process the highest priority interrupt
    switch (iid & UIIR_ID_MASK)
      {
      case UIIR_RLS_INT:                // Receive Line Status
        U1LSR;                          // read LSR to clear
        break;

#ifdef UART1_RX_INT_MODE
      case UIIR_CTI_INT:                // Character Timeout Indicator
      case UIIR_RDA_INT:                // Receive Data Available
        do
          {
          uint16_t temp;
          // calc next insert index & store character
          temp = (uart1_rx_insert_idx + 1) % UART1_RX_BUFFER_SIZE;
          uart1_rx_buffer[uart1_rx_insert_idx] = U1RBR;

          // check for more room in queue
          if (temp != uart1_rx_extract_idx)
            uart1_rx_insert_idx = temp; // update insert index
          }
        while (U1LSR & ULSR_RDR);

        break;
#endif

#ifdef UART1_TX_INT_MODE
      case UIIR_THRE_INT:               // Transmit Holding Register Empty
        while (U1LSR & ULSR_THRE)
          {
          // check if more data to send
          if (uart1_tx_insert_idx != uart1_tx_extract_idx)
            {
            U1THR = uart1_tx_buffer[uart1_tx_extract_idx++];
            uart1_tx_extract_idx %= UART1_TX_BUFFER_SIZE;
            }
          else
            {
            // no
            uart1_tx_running = 0;       // clear running flag
            break;
            }
          }

        break;
#endif // UART1_TX_INT_MODE

      case UIIR_MS_INT:                 // MODEM Status
        U1MSR;                          // read MSR to clear
        break;

      default:                          // Unknown
        U1LSR;
        U1RBR;
        U1MSR;
        break;
      }
    }

  VICVectAddr = 0x00000000;             // clear this interrupt from the VIC
  ISR_EXIT();                           // recover registers and return
}
