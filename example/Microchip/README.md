## About demo applications
The sources, provided in the [./Applications](./Applications/) directory give a recommended usage of each block type.  
Read the comments in the files for details.  

### Prerequisites
[`uv`](../../README.md#prerequisites) has to be installed - the pre-build actions use it to run the code generator.  
For full source code availability, You'll need to generate the _mEEM_'s configuration manually, by executing [pre-build actions.ps1](./mEEM-demo.X/pre_build_actions.ps1).   
Or, if You already have `MPLAB X` IDE and `XC8` compiler installed, simply open and build the `mEEM-demo.X` project.  
The pre-build actions run the code generator without legacy models, because the demo has no earlier releases to migrate from. [Here](../../doc/Configuration.md#4-give-the-generator-your-earlier-releases) is what a product adds to them.