# 

FlowCoro

## 



### 1. Python ()
- ****: `remove_emojis.py`
- ****: Unicode
- ****: Python 3.6+

### 2. Bash
- ****: `remove_emojis.sh`
- ****: 
- ****: Bash shell

## 

### 

```bash
# Python - 
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py

# Bash - 
cd /home/caixuf/MyCode/flowcord
./scripts/remove_emojis.sh
```

### 

```bash
# Python - 
python3 scripts/remove_emojis.py --apply

# Bash - 
./scripts/remove_emojis.sh --apply
```

### 

```bash
# Python - 
python3 scripts/remove_emojis.py --apply --backup

# Bash - 
./scripts/remove_emojis.sh --apply --backup
```

## 

### Python

```bash
python3 scripts/remove_emojis.py []

:
  --dry-run 
  --apply --dry-run
  --backup 
  --directory DIR 
  --help 
```

### Bash

```bash
./scripts/remove_emojis.sh []

:
  --apply 
  --backup 
  --verbose, -v 
  --help, -h 
```

## 



- **C++**: `*.cpp`, `*.hpp`, `*.h`, `*.c`, `*.cc`, `*.cxx`
- ****: `*.md`, `*.txt`, `*.rst`
- ****: `*.cmake`, `CMakeLists.txt`
- ****: `*.py`, `*.sh`, `*.bash`
- ****: `*.json`, `*.yaml`, `*.yml`
- ****: `*.log`

## 

### Python
- Unicode
- 
- FlowCoro

### Bash
- 
  -
  -
  -
  - ...

## 

1. ****: 
2. ****: 
3. ****: 
4. ****:  `.git`, `build`, `.cache` 

## 

### 1: 

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --verbose
```

### 2: 

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --apply --backup
```

### 3: 

```bash
cd /home/caixuf/MyCode/flowcord
python3 scripts/remove_emojis.py --apply --directory ./docs
```

### 4: Bash

```bash
cd /home/caixuf/MyCode/flowcord
./scripts/remove_emojis.sh --apply --verbose
```

## 

```
 FlowCoro 
==============================
: /home/caixuf/MyCode/flowcord
: 
: 

 : /home/caixuf/MyCode/flowcord
============================================================
: /home/caixuf/MyCode/flowcord/README.md
    25 
   []  25 

: /home/caixuf/MyCode/flowcord/docs/USAGE.md
    12 
   []  12 

============================================================
 
============================================================
: 45
: 8
: 89

 
  --apply 
```

## 

1. ****: 
2. ****:  `--backup` 
3. ****: PythonUTF-8GB2312
4. ****: 

## 

 `--backup` 

```bash
# 
mv file.txt.bak file.txt

# 
find . -name "*.bak" -exec sh -c 'mv "$1" "${1%.bak}"' _ {} \;
```
