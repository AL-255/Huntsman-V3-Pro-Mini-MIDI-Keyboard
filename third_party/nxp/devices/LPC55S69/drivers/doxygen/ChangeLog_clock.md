# CLOCK

## [2.3.8]

- Bug Fixes
  - Fixed an issue that ss_progmodfm_t, ss_progmoddp_t, and ss_modwvctrl_t use wrong shift value.

## [2.3.7]

- Improvements
  - Add errata workaround for pll lock bit in CLOCK_SetPLL0Freq() and CLOCK_SetPLL1Freq().

## [2.3.6]

- Bug Fixes
  - Correct the fail status condition in CLOCK_SetupExtClocking().

## [2.3.5]

- Improvements
  - Added lost comments for some enumerations.

## [2.3.4]

- Bug Fixes
  - Fix CLOCK_SetClkDiv function to set kCLOCK_DivFlexFrgx correctly.
  - Fixed violations of MISRA C-2012 rule 16.3, rule 10.4, rule 10.3.

## [2.3.3]

- Bug Fixes
  - Fix the pllRate in CLOCK_SetPLL1Freq().

## [2.3.2]

- Improvements
  - Optimized the CLOCK_GetPLL0OutFromSetup() function with the usage of input parameter.

## [2.3.1]

- Bug Fixes
  - Fixed MISRA C-2012 rule 10.1, rule 12.2, rule 14.4 and so on.
  - Fixed IAR warning Pa082 for the clock driver.

## [2.3.0]

- New Features
  - Moved SDK_DelayAtLeastUs function from clock driver to common driver.

## [2.2.2]

- Bug Fixes
  - Corrected the PLL.SELI setting to align with new UM.
  - Changed the PLL lock reliable condition.

## [2.2.1]

- Improvements
  - Removed redundant macro definitions.

## [2.2.0]

- New Features
  - Added CLOCK_SetupPLUClkInClocking() to store the PLU CLKIN frequency.

## [2.1.1]

- Improvements
  - Updated CLOCK_SetFLASHAccessCyclesForFreq() to support up to 150MHz frequency.

## [2.1.0]

- New Features
  - Added new API CLOCK_DelayAtLeastUs() implemented by DWT to allow users to set delay in unit of microsecond.

## [2.0.4]

- Bug Fixes
  - Fixed C++ build errors in CLOCK_GetClockAttachId() and CLOCK_AttachClk().

## [2.0.3]

- Bug Fixes
  - Fixed attach incorrect attach_id.

## [2.0.2]

- New Features
  - Added get actual clock attach id API to allow users to obtain the actual
    clock source in target register.
- Bug Fixes
  - The attach clock and get actual clock attach id APIs should check
    combination of two clock source.
- Optimization
  - Made the judgement statements more clear.
  - Strengthened the compatibility of clock attach id.
  - Removed some non-meaningful definitions and add some useful ones to
    enhance readability.

## [2.0.1]

- Some minor fixes.

## [2.0.0]

- initial version.
