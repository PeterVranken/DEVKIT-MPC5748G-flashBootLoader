The script generateRandomSrecFile generates randomly filled s-record
files, which can be used for testing of the FBL. 

The code has been written by Copilot. Requirements were:

Hi Copilot! For testing of my FBL, I would require a script, which can
generate s-record files. Arguments should be: 
- Name of created file
- Text for a single contains s0 record.
- A list of address ranges [from, till), where from and till are
  hexadecimal numbers. 
Data contents should be chosen randomly.

The script language could be PowerShell, Java (as a class with a main
method) or a little plain C file. 
