# Contributing to shasta

Contributions of code, ideas, computational experiments,
or documentation are welcome. 
To contribute, please use the standard GitHub Pull Request process
and take into consideration the suggestions below.
To facilitate integration of your code,
use a new branch for each Pull Request.
You can choose any name you like for the branch you create for this purpose,
as long as it does not conflict with the name of an existing branch.



#### Contributing C++ code

* Add at least a minimal amount
of comments to explain or clarify what the code does. 
* Use blank lines generously to separate code blocks.
* Set your editor to convert tabs to 4 spaces.
* To minimize name pollution of the global namespace,
all code should be in the `ChanZuckerberg::shasta` namespace.
Do not use `using` directives at global scope.
* If you need to add a large amount of code to an existing
file, consider creating a new file instead.
* Keep in mind that the code is currently compiled
using the C++11 standard. Use features in that standard
liberally. However, you cannot use C++ features
that were introduced in the C++14 or C++17 standard.
* For functionality not provided
by the standard libraries, you can also use the Boost libraries.
* If possible, avoid adding dependencies on other packages.



#### Contributing ideas, computational experiments, comments, or criticism
* Please use the GitHub 
[Wiki](https://github.com/chanzuckerberg/shasta/wiki)
or 
[Issues](https://github.com/chanzuckerberg/shasta/issues)
sections of the GitHub repository as appropriate.



#### Contributing documentation

* Put your contribution in the `docs`
directory, so it becomes automatically visible in GitHub Pages.
* Feel free to create new `.html` files, if appropriate.
* Attempt to harmonize your new materials with the existing
documentation, if possible.

