const dependencies = {};
const plugs = {};
const operatingSystems = {};
const profiles = [];
let selectedOS = "";
const plugins_linux = new Set();
const plugins_windows = new Set();
const plugins_android = new Set();
let windows_build = false;

function makeCopyable(code, name) {
    return "<div class=\"code-box-copy\">\n<button class=\"code-box-copy__btn\" data-clipboard-target=\"#" + name + "\" title=\"Copy\">\n</button>\n<pre class=\"language-shell\" id=\"" + name + "\">" + code + "</pre>\n</div>\n";
}

function loadModules() {
    for (let os of module_json["systems"]) {
        operatingSystems[os.name] = os.versions;
    }

    for (let item of module_json["dependencies"]) {
        let nm = "";
        for (let i in item) {
            nm = i;
        }
        dependencies[nm] = {
            'pkg': item[nm].pkg,
            'plugins': []
        };
    }
    for (let item of module_json["plugins"]) {
        plugs[item.name] = item.cmake_flag;
        if (item.OS.includes("linux")) {
            plugins_linux.add(item.name);
            document.getElementById("linux_plugins").innerHTML += "<li>" + item.name + "</li>";
        }
        for (let dep of item.dependencies) {
            dependencies[dep].plugins.push(item.name);
        }
        if (item.OS.includes("windows")) {
            plugins_windows.add(item.name);
            document.getElementById("windows_plugins").innerHTML += "<li>" + item.name + "</li>";
        }
        if (item.OS.includes("android")) {
            
        }
    }
    for (let grp of module_json["profiles"]) {
        profiles.push(grp.name);
        profiles[grp.name] = [];
        for (let plug of grp.plugins) {
            profiles[grp.name].push(plug);
        }
    }
}

function updateSudo() {
    let cmakeLine;
    if (windows_build) {
        cmakeLine = "";
        for (let p of plugins_windows) {
            if (document.getElementById(p + "_win").checked) {
                cmakeLine += " -D" + plugs[p];
            }
        }
        if (document.getElementById("no_gpl").checked) {
            cmakeLine += " -DNO_GPL=ON";
            if (document.getElementById("no_lgpl").checked) {
                cmakeLine += " -DNO_LGPL=ON";
            }
        }

        document.getElementById("cmake_config").innerHTML = makeCopyable(cmakeLine, "cmakeConfig");

    } else {
        let sudoLine = "";
        let notes = document.getElementById("notes");
        let pkgnotes = "";
        let pkginfo = "";
        let postnotes = document.getElementById("postnotes");
        let depinstall = document.getElementById("depinstall");

        notes.innerHTML = "";

        if (selectedOS === "Ubuntu") {
            sudoLine = "sudo apt-get install libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl-dev pkg-config libopencv-dev libeigen3-dev libc6-dev libspdlog-dev libboost-all-dev git cmake cmake-data";
            postnotes.innerHTML = "";
        } else {
            sudoLine = "sudo dnf install glew-devel mesa-libGLU-devel sqlite-devel libX11-devel mesa-libGL-devel pkgconf-pkg-config opencv-devel eigen3-devel glibc-devel spdlog-devel boost-devel git cmake cmake-data";
            postnotes.innerHTML = "Potential issues:<ul><li>If cmake is having trouble with some of the <package-confi></package-confi>g (.pc) files used to locate packages you may need to run the following:" + makeCopyable("sudo sed -i 's/\^\[ \\t\]\*//g' /usr/lib64/pkgconfig/*", "postF") + "</li><li>If the build step is having issues finding some of the include files, you may need the following:" + makeCopyable("sudo ln -s /usr/include /include", "post2F") + "</li></ul>";
        }

        for (let m in dependencies) {
            let checked = false;
            for (let p of dependencies[m].plugins) {
                checked ||= document.getElementById(p).checked;
            }
            if (checked) {
                sudoLine += " " + dependencies[m].pkg[selectedOS].pkg;
                if (dependencies[m].pkg[selectedOS].postnotes !== "") {
                    pkgnotes += "<P>" + dependencies[m].pkg[selectedOS].postnotes;
                }
                if (dependencies[m].pkg[selectedOS].notes !== "") {
                    if (pkginfo !== "") {
                        pkginfo += "<P>";
                    }
                    pkginfo += dependencies[m].pkg[selectedOS].notes;
                }
            }
        }

        if (document.getElementById("withVirt").checked) {
            sudoLine += " " + dependencies["qemu"].pkg[selectedOS].pkg;
        }

        if (pkginfo !== "") {
            notes.innerHTML = "<h3>Notes:</h3>" + pkginfo;
        }

        if (selectedOS === "Ubuntu" && (document.getElementById("offload_vio.device_tx").checked ||
            document.getElementById("offload_vio.device_rx").checked ||
            document.getElementById("offload_vio.server_tx").checked ||
            document.getElementById("offload_vio.server_rx").checked)) {
            pkgnotes += "<P><br>For the offload_vio plugins, it is strongly recommended to install DeepStream. Please see the installation instructions <a href=\"https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_Installation.html#dgpu-setup-for-ubuntu\">here</a>.";
        }

        depinstall.innerHTML = pkgnotes;

        document.getElementById("output").innerHTML = sudoLine;

        cmakeLine = "cd ILLIXR\nmkdir build\ncd build\ncmake .. -DCMAKE_INSTALL_PREFIX=&lt;LOCATION&gt;";
        let profile_check = false;

        for (let g of profiles) {
            if (document.getElementById("profile_" + g).checked) {
                cmakeLine += " -DYAML_FILE=profiles/" + g.toLowerCase() + ".yaml";
                profile_check = true;
                break;
            }
        }

        if (!profile_check) {
            for (let p of plugins) {
                if (document.getElementById(p).checked) {
                    cmakeLine += " -D" + plugs[p];
                }
            }
        }
        if (document.getElementById("no_gpl").checked) {
            cmakeLine += " -DNO_GPL=ON";
            if (document.getElementById("no_lgpl").checked) {
                cmakeLine += " -DNO_LGPL=ON";
            }
        }
        cmakeLine += "\ncmake --build . -j4\ncmake --build . -t docs   # if you want to build the documentation\ncmake --install .";
        document.getElementById("cmake").innerHTML = makeCopyable(cmakeLine, "cmakeC");
    }
    $('.code-box-copy').codeBoxCopy();
}

function updateGPL() {
    if (!document.getElementById("no_gpl").checked) {
        document.getElementById("no_lgpl").checked = false;
    }
    updateSudo();
}

function updateLGPL() {
    if (document.getElementById("no_lgpl").checked) {
        document.getElementById("no_gpl").checked = true;
    }
    updateSudo();
}

function updateOS(os_str) {
    const items = os_str.split(".");
    selectedOS = items[0];
    updateSudo();
}

function checkAll() {
    for (let p of plugins) {
        document.getElementById(p).checked = document.getElementById("ALL_plugins").checked;
    }
    updateSudo();
}

function checkProfile(profile_name) {
    for (let p of plugins) {
        let setChecked = false;
        for (let pl of profiles[profile_name]) {
            if (p === pl) {
                setChecked = true;
                break;
            }
        }
        document.getElementById(p).checked = setChecked;
    }
    updateSudo();
}

function updateChecked(loc) {
    windows_build = loc === 1;
    document.getElementById("None_plugins").checked = true;
    updateSudo();
}

async function setUpPage() {
    loadModules();
    let osref = document.getElementById("operating_system");
    let osrow = osref.insertRow(-1);
    for (let x in operatingSystems) {
        let txt = x + ":";
        for (let ver of operatingSystems[x]) {
            txt += "<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<input type='radio' id='" + x + "' name='os_choice' value='" + x + "' onclick='updateOS(this.value);'>" + ver;
        }
        let cell = osrow.insertCell(-1);
        cell.style.verticalAlign = "top";
        cell.innerHTML = txt;
    }
    selectedOS = "Ubuntu";
    document.getElementById(selectedOS).checked = true;
    let tabRef = document.getElementById("profile_table");

    let count = 0;
    let currentRow = tabRef.insertRow(-1);

    let profilecell = currentRow.insertCell(-1);
    profilecell.innerHTML = "<b>Profiles:</b>";
    profilecell.setAttribute("colspan", profiles.length + 2);
    currentRow = tabRef.insertRow(-1);

    for (let g of profiles) {
        if (count >= 3) {
            currentRow = tabRef.insertRow(-1);
            count = 0;
        }
        let cell = currentRow.insertCell(-1);
        let radio = document.createElement("INPUT");
        radio.setAttribute("type", "radio");
        radio.setAttribute("onclick", "checkProfile('" + g + "');");
        radio.setAttribute("id", "profile_" + g);
        radio.setAttribute("value", "profile_" + g);
        radio.setAttribute("name", "profile_selection");
        let label = document.createElement("LABEL");
        label.setAttribute("for", "profile_" + g);
        label.appendChild(document.createTextNode(g));
        cell.appendChild(radio);
        cell.appendChild(label);
        count++;
    }
    count = 0;
    let none_cell = currentRow.insertCell(-1);
    let none_check = document.createElement("INPUT");
    none_check.setAttribute("type", "radio");
    none_check.setAttribute("onclick", "updateSudo();");
    none_check.setAttribute("id", "None_plugins");
    none_check.setAttribute("value", "None_plugins");
    none_check.setAttribute("name", "profile_selection");
    none_check.checked = true;
    let none_label = document.createElement("LABEL");
    none_label.setAttribute("for", "None_plugins");
    none_label.appendChild(document.createTextNode("None"));
    none_cell.appendChild(none_check);
    none_cell.appendChild(none_label);

    tabRef = document.getElementById("listing_table");
    currentRow = tabRef.insertRow(-1);
    for (const dep of plugins_linux) {
        if (count >= 3) {
            currentRow = tabRef.insertRow(-1);
            count = 0;
        }
        let cell = currentRow.insertCell(-1);
        let x = document.createElement("INPUT");
        x.setAttribute("type", "checkbox");
        x.setAttribute("onclick", "updateChecked(0);");
        x.setAttribute("id", dep);
        let y = document.createElement("LABEL");
        y.setAttribute("for", dep);
        y.appendChild(document.createTextNode(dep));
        cell.appendChild(x);
        cell.appendChild(y);
        count += 1;
    }
    let winRef = document.getElementById("listing_table_windows");
    count = 0;
    currentRow = winRef.insertRow(-1);
    for (const dep of plugins_windows) {
        if (count >= 3) {
            currentRow = winRef.insertRow(-1);
            count = 0;
        }
        let cell = currentRow.insertCell(-1);
        let x = document.createElement("INPUT");
        x.setAttribute("type", "checkbox");
        x.setAttribute("onclick", "updateChecked(1);");
        x.setAttribute("id", dep + "_win");
        let y = document.createElement("LABEL");
        y.setAttribute("for", dep + "_win");
        y.appendChild(document.createTextNode(dep));
        cell.appendChild(x);
        cell.appendChild(y);
        count += 1;
    }
    
    let andrRef = document.getElementById("listing_table_android");
    for (const dep of plugins_android) {
        const li = document.createElement("li");
        li.textContent = dep;
        andRef.appendChild(li);
    }
    updateSudo();
}
window.onload = setUpPage();
