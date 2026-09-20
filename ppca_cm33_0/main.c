/*******************************************************************************
* File Name:   main.c
*
* Description: This is the main file for the PPCA CPU core 0. This file contains
* the main function for the core.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "cy_pdl.h"
#include "cycfg.h"
    
/******************************************************************************
* Macros
*******************************************************************************/
/* Shared memory addresses in M4 shared memory space (0x20040000-0x20043FFF) */
/* All cores can access M4 shared memory for inter-core communication */
#define PPCA_CPU0_M4_VAR_ADDRESS 0x20040400  /* Used by this core (CPU0) */

/* Shared memory addresses in M4 shared memory space (0x20040000-0x20043FFF) */
/* All cores can access M4 shared memory for inter-core communication */
#define PPCA_CPU1_M4_VAR_ADDRESS 0x20040800  /* Used by this core (CPU1) */

/*******************************************************************************
* Global Variables
*******************************************************************************/
int32_t *adc_data           = (int32_t *)PPCA_CPU0_M4_VAR_ADDRESS;
int32_t *hw_filter_out      = (int32_t *)PPCA_CPU0_M4_VAR_ADDRESS + 1;
int32_t *core0_data_updated = (int32_t *)PPCA_CPU0_M4_VAR_ADDRESS + 2;

cy_stc_sysint_t adc_intr_config =
{
    .intrSrc = EPU_IRQ_EPU_0,
    .intrPriority = 1U,
};

cy_stc_sysint_t hwfilter_intr_config =
{
    .intrSrc = EPU_IRQ_EPU_1,
    .intrPriority = 1U,
};

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void adc_isr();
void hwfilter_isr();

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: main
*********************************************************************************
* Summary:
* This is the main function for PPCA CPU 0. It performs the initialization of the
* variables used in the code. Also, it reads the ADC read data, ADC filter data
* and Hardware filter data in periodic intervals and copies the date to the
* shared memory for consuming by the main CPU.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
     /* Configuring EPU processing unit to receive terminal count signal from PWM. */
     Cy_PPCA_EPU_PU_T1_Configure(ADC_EOP_HW, ADC_EOP_INDEX, &ADC_EOP_put1_config);
     Cy_PPCA_EPU_PU_T1_Enable(ADC_EOP_HW, ADC_EOP_INDEX, ADC_EOP_ENABLE_MODE);

     /* Configuring EPU processing unit to receive terminal count signal from PWM. */
     Cy_PPCA_EPU_PU_T1_Configure(HWFILTER_EOP_HW, HWFILTER_EOP_INDEX, &HWFILTER_EOP_put1_config);
     Cy_PPCA_EPU_PU_T1_Enable(HWFILTER_EOP_HW, HWFILTER_EOP_INDEX, HWFILTER_EOP_ENABLE_MODE);

     /* Configuring EPU combiner to route the signal from PWM to ADC to start a conversion*/
     Cy_PPCA_EPU_Combo_Configure(ADC_EOP_INTR_HW, ADC_EOP_INTR_INDEX, &ADC_EOP_INTR_combo_config);
     /* Configuring EPU combiner to route the signal from PWM to ADC to start a conversion*/
     Cy_PPCA_EPU_Combo_Configure(HWFILTER_EOP_INTR_HW, HWFILTER_EOP_INTR_INDEX, &HWFILTER_EOP_INTR_combo_config);

     /* Enabling EPU Core 0 interrupt to received interrupt from ADC. */
     Cy_PPCA_EPU_InterruptSourceSelect(EPU_EPU_IRQ0_HW, false, epuIrqSrc0);
     Cy_PPCA_EPU_SetInterruptMask(EPU_EPU_IRQ0_HW);

     /* Enabling EPU Core 0 interrupt to received interrupt from HW Filter. */
     Cy_PPCA_EPU_InterruptSourceSelect(EPU_EPU_IRQ1_HW, false, epuIrqSrc1);
     Cy_PPCA_EPU_SetInterruptMask(EPU_EPU_IRQ1_HW);

     /* Configuring ADC conversion completion ISR in CPU core. */
     Cy_SysInt_Init(&adc_intr_config, &adc_isr);
     NVIC_EnableIRQ(adc_intr_config.intrSrc);

     /* Configuring HW filter processing completion ISR in CPU core. */
     Cy_SysInt_Init(&hwfilter_intr_config, &hwfilter_isr);
     NVIC_EnableIRQ(hwfilter_intr_config.intrSrc);

    __enable_irq();

    /* Starting PWM */
    Cy_TCPWM_TriggerStart_Single(PWM_HW, PWM_NUM);

    for(;;)
     {
          Cy_SysLib_Delay(250);
     }
}

/*******************************************************************************
* Function Name: adc_isr
*********************************************************************************
* Summary:
* This is the interrupt service routine triggered after completing the ADC 
* conversion. It reads ADC data and writes it into the HW Filter input.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
void adc_isr()
{
    /* Clearing the interrupt in EPU */
    Cy_PPCA_EPU_ClearInterrupt(EPU_EPU_IRQ0_HW);

    /* Reading ADC data. Shifting it to make 16bit for HW filter */
    *adc_data    = Cy_PPCA_ADC_Read_ADC_Data(ADC_HW, 0) << 4;

    /* Writing the data to the HW Filter input */
    Cy_PPCA_HWFILT3P3Z_Write_DATA_IN1(HWFILTER_HW, *adc_data);

    /* Flag to inform the Main core that a new data is available. */
    *core0_data_updated = 1;
}

/*******************************************************************************
* Function Name: hwfilter_isr
*********************************************************************************
* Summary:
* This is the interrupt service routine triggered after completing the HW Filter 
* processing. It reads HW filter output and writes it into the PWM compare 0 
* register.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
void hwfilter_isr()
{
    /* Clearing the interrupt in EPU */
    Cy_PPCA_EPU_ClearInterrupt(EPU_EPU_IRQ1_HW);

    /* Reading HW filter output */
    *hw_filter_out = Cy_PPCA_HWFILT3P3Z_ReadFilterDataOutput(HWFILTER_HW);

    /* Writing the HW Filter output to the PWM compare buffer */
    Cy_TCPWM_PWM_SetCompare0(PWM_HW, PWM_NUM, *hw_filter_out);

    /* Flag to inform the Main core that a new data is available. */
    *core0_data_updated = 1;
}
