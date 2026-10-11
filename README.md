# BÁO CÁO GIỮA KỲ MÔN LẬP TRÌNH HỆ THỐNG
## ĐỀ TÀI: TÁI TẠO TIỆN ÍCH `ls` THEO ĐẶC TẢ NETBSD 10.1 (POSIX.1-2008)

- **Sinh viên thực hiện:** Ngô Quang Minh
- **Mã sinh viên:** 24IT162
- **Lớp / Học phần:** Lập trình Hệ thống (System Programming)
- **Chuẩn đặc tả tham chiếu:** NetBSD 10.1 General Commands Manual (`ls(1)`) & IEEE Std 1003.1-2008 (POSIX.1-2008)
- **Môi trường thực thi & kiểm thử:** Linux POSIX Environment (Ubuntu 24.04 LTS / WSL2) & GCC 13+

---

## MỤC LỤC
1. [Tổng quan Đề tài và Kiến trúc Module](#1-tổng-quan-đề-tài-và-kiến-trúc-module)
2. [Phân tích Chuyên sâu các System Calls và POSIX APIs Cốt lõi](#2-phân-tích-chuyên-sâu-các-system-calls-và-posix-apis-cốt-lõi)
   - 2.1. Quét cấu trúc thư mục: `opendir()`, `readdir()`, `closedir()`
   - 2.2. Trích xuất Metadata: `lstat()` đối chiếu với `stat()`
   - 2.3. Đọc liên kết mềm: `readlink()`
   - 2.4. Ánh xạ danh tính người dùng và nhóm: `getpwuid()`, `getgrgid()`
   - 2.5. Giải mã phân quyền và bit đặc biệt: Bitwise Operations trên `st_mode`
   - 2.6. Định dạng thời gian và quy tắc 6 tháng: `strftime()`
   - 2.7. Nhận diện thiết bị ngoại vi: `major()` và `minor()`
3. [Giải thích Chuyên sâu: Cơ chế `$PATH` và Lý do Thực thi `./my_ls`](#3-giải-thích-chuyên-sâu-cơ-chế-path-và-lý-do-thực-thi-my_ls)
   - 3.1. Cơ chế tìm kiếm và phân giải tên lệnh của UNIX Shell
   - 3.2. Triết lý thiết kế An toàn (Security by Design) trong UNIX
   - 3.3. Phân tích hiểm họa bảo mật: PATH Hijacking & Trojan Injection
   - 3.4. Bản chất cú pháp của tiền tố `./`
4. [Bảng Đặc tả 19 Cờ lệnh và Quy tắc Ghi đè (Override Rules)](#4-bảng-đặc-tả-19-cờ-lệnh-và-quy-tắc-ghi-đè-override-rules)
5. [Kết quả Kiểm thử Thực nghiệm Đối chiếu (Verification Results)](#5-kết-quả-kiểm-thử-thực-nghiệm-đối-chiếu-verification-results)
6. [Hướng dẫn Biên dịch và Sử dụng](#6-hướng-dẫn-biên-dịch-và-sử-dụng)

---

## 1. TỔNG QUAN ĐỀ TÀI VÀ KIẾN TRÚC MODULE

### 1.1. Mục tiêu đề tài
Dự án nhằm tái lập hoàn chỉnh tiện ích liệt kê nội dung thư mục `ls` theo đúng đặc tả của hệ điều hành NetBSD phiên bản 10.1 (`ls(1)` manual). Chương trình được viết bằng ngôn ngữ C chuẩn C11, tuân thủ các quy tắc lập trình POSIX.1-2008, không phụ thuộc vào các thư viện bên ngoài và được kiểm soát lỗi nghiêm ngặt với các cờ biên dịch `-Wall -Wextra -Werror -pedantic`.

### 1.2. Cấu trúc mã nguồn module hóa
Dự án được phân tách thành các module chức năng độc lập theo chuẩn công nghiệp:

```
.
├── Makefile                # Build system tự động (all, clean, test, debug)
├── include/
│   └── ls.h                # Header định nghĩa types, structs, prototypes toàn cục
├── src/
│   ├── main.c              # Entry point, parse_options (getopt), xử lý operands
│   ├── file_info.c         # Thu thập metadata (lstat, readlink), format mode & size
│   ├── sort.c              # Thuật toán sắp xếp đa tiêu chí (name, size, time, reverse)
│   ├── display.c           # Định dạng căn lề cột, in long/short listing, total blocks
│   └── traverse.c          # Duyệt thư mục (opendir/readdir), lọc dotfiles, đệ quy -R
├── tests/
│   └── test_runner.sh      # Test suite tự động 43 test cases so sánh với system ls
└── README.md               # Báo cáo kỹ thuật chi tiết và tài liệu dự án
```

---

## 2. PHÂN TÍCH CHUYÊN SÂU CÁC SYSTEM CALLS VÀ POSIX APIS CỐT LÕI

Để tương tác với hệ thống tập tin tại tầng nhân (Kernel Space), chương trình sử dụng tập hợp các System Calls và POSIX C Library wrappers cơ bản sau:

### 2.1. Quét cấu trúc thư mục: `opendir()`, `readdir()`, `closedir()`
- **Khái niệm:** Thư mục trong UNIX thực chất là một tập tin đặc biệt chứa danh sách các bản ghi (directory entries), mỗi bản ghi ánh xạ một tên tập tin (filename string) tới một chỉ số inode tương ứng trên hệ thống tập tin.
- **`opendir(const char *name)`:**
  - Nhận vào đường dẫn thư mục, mở file descriptor tương ứng tại tầng kernel và khởi tạo một luồng thư mục `DIR *`.
  - Trả về con trỏ `DIR *` nếu thành công, hoặc `NULL` kèm `errno` (`EACCES` nếu không có quyền đọc, `ENOENT` nếu không tồn tại thư mục).
- **`readdir(DIR *dirp)`:**
  - Đọc tuần tự từng bản ghi thư mục, trả về con trỏ tới `struct dirent`.
  - Cấu trúc `struct dirent` chứa trường cốt lõi `d_name` (chuỗi ký tự tên file).
  - Khi đọc hết thư mục hoặc gặp lỗi, `readdir()` trả về `NULL`.
- **`closedir(DIR *dirp)`:**
  - Đóng luồng thư mục và giải phóng file descriptor cùng vùng đệm nội bộ của kernel, ngăn ngừa cạn kiệt tài nguyên file descriptor (file descriptor leak).

### 2.2. Trích xuất Metadata: `lstat()` đối chiếu với `stat()`
- **Tầm quan trọng sống còn:**
  - `stat(const char *pathname, struct stat *statbuf)`: Khi gặp tập tin là symbolic link, `stat()` sẽ tự động giải tham chiếu (dereference) và trả về thông tin của **tập tin đích** mà liên kết trỏ tới.
  - `lstat(const char *pathname, struct stat *statbuf)`: Khi gặp symbolic link, `lstat()` trả về thông tin của **chính tập tin liên kết mềm đó** (inode của link, kích thước chuỗi path của link, quyền của link).
- **Tại sao `ls` bắt buộc phải dùng `lstat()`?**
  1. Nếu dùng `stat()`, cờ phân loại kiểu file sẽ luôn nhận diện symbolic link thành regular file hoặc directory của đích đến, dẫn đến việc không thể hiển thị ký tự `l` ở đầu chuỗi quyền hạn.
  2. Khi gặp một **broken symlink** (liên kết mềm trỏ tới một file không tồn tại), `stat()` sẽ thất bại và trả về lỗi `ENOENT` (No such file or directory), khiến chương trình tưởng file không tồn tại. Trong khi đó, `lstat()` vẫn thành công bình thường và đọc được metadata của symlink.
  3. Ngăn chặn hiện tượng lặp vô tận (infinite recursion loop) khi người dùng tạo symbolic link trỏ ngược về thư mục cha hoặc thư mục tổ tiên (`link -> ..`).

### 2.3. Đọc liên kết mềm: `readlink()`
- **Hàm `ssize_t readlink(const char *pathname, char *bufsiz, size_t bufsiz)`:**
  - Đọc nội dung chuỗi đường dẫn đích chứa bên trong symbolic link.
  - **Lưu ý kỹ thuật đặc biệt:** `readlink()` **không bao giờ tự động thêm byte `\0` (null-terminator)** vào cuối chuỗi đích trong buffer. Lập trình viên phải tự gán `buf[len] = '\0'` dựa trên giá trị trả về của `readlink` để tránh tràn bộ đệm đọc (buffer overrun) khi in chuỗi.

### 2.4. Ánh xạ danh tính người dùng và nhóm: `getpwuid()`, `getgrgid()`
- Trong inode (`struct stat`), quyền sở hữu tập tin chỉ được lưu trữ dưới dạng số nguyên không âm `uid_t st_uid` và `gid_t st_gid`.
- Để hiển thị tên người dùng và nhóm dạng chuỗi thân thiện (`megumi`, `root`):
  - `struct passwd *getpwuid(uid_t uid)`: Tra cứu cơ sở dữ liệu người dùng hệ thống (tập tin `/etc/passwd` hoặc NSS/LDAP) và trả về cấu trúc chứa chuỗi `pw_name`.
  - `struct group *getgrgid(gid_t gid)`: Tra cứu cơ sở dữ liệu nhóm hệ thống (tập tin `/etc/group`) và trả về cấu trúc chứa chuỗi `gr_name`.
- **Cơ chế dự phòng (Fallback):** Nếu hệ thống không tìm thấy tên tương ứng (ví dụ tập tin thuộc về người dùng đã bị xóa), chương trình tự động chuyển sang hiển thị định dạng số (`sprintf("%u", st_uid)`).

### 2.5. Giải mã phân quyền và bit đặc biệt: Bitwise Operations trên `st_mode`
Trường `st_mode` (kiểu `mode_t` - thường là số nguyên 16-bit hoặc 32-bit) mã hóa cả kiểu file lẫn phân quyền truy cập:

```
+---------------+-------+---+---+---+---+---+---+---+---+---+
| Kiểu tập tin  | SUID  | r | w | x | r | w | x | r | w | x |
| (4 bits)      | SGID  | (Owner)   | (Group)   | (Other)   |
|               |Sticky |           |           |           |
+---------------+-------+---+---+---+---+---+---+---+---+---+
```

- **Mặt nạ kiểu tập tin `S_IFMT`:**
  - `(st_mode & S_IFMT) == S_IFREG` tương đương macro `S_ISREG(st_mode)` (Regular file: `-`)
  - `(st_mode & S_IFMT) == S_IFDIR` tương đương macro `S_ISDIR(st_mode)` (Directory: `d`)
  - `(st_mode & S_IFMT) == S_IFLNK` tương đương macro `S_ISLNK(st_mode)` (Symbolic link: `l`)
  - `(st_mode & S_IFMT) == S_IFCHR` tương đương macro `S_ISCHR(st_mode)` (Character device: `c`)
  - `(st_mode & S_IFMT) == S_IFBLK` tương đương macro `S_ISBLK(st_mode)` (Block device: `b`)
  - `(st_mode & S_IFMT) == S_IFIFO` tương đương macro `S_ISFIFO(st_mode)` (FIFO/pipe: `p`)
  - `(st_mode & S_IFMT) == S_ISSOCK` tương đương macro `S_ISSOCK(st_mode)` (Socket: `s`)
- **Các bit quyền đặc biệt (Special Permissions):**
  - **Set-User-ID (`S_ISUID`):** Tại vị trí ký tự thứ 3 của Owner:
    - Nếu file có quyền execute (`S_IXUSR`): in `s`.
    - Nếu file không có quyền execute: in `S`.
  - **Set-Group-ID (`S_ISGID`):** Tại vị trí ký tự thứ 6 của Group:
    - Nếu file có quyền execute (`S_IXGRP`): in `s`.
    - Nếu file không có quyền execute: in `S`.
  - **Sticky Bit (`S_ISVTX`):** Tại vị trí ký tự thứ 9 của Other:
    - Thường áp dụng cho thư mục dùng chung như `/tmp` (chỉ chủ sở hữu file mới có quyền xóa file của mình).
    - Nếu file có quyền execute (`S_IXOTH`): in `t`.
    - Nếu file không có quyền execute: in `T`.

### 2.6. Định dạng thời gian và quy tắc 6 tháng: `strftime()`
- Chuẩn POSIX quy định định dạng hiển thị ngày giờ trong cờ `-l` phụ thuộc vào khoảng cách thời gian giữa thời điểm hiện tại và timestamp của file:
  - Nếu thời gian tập tin nằm trong khoảng **6 tháng qua** (khoảng 180 ngày) và không nằm trong tương lai:
    Sử dụng định dạng `"%b %e %H:%M"` (Ví dụ: `Oct 11 14:15`).
  - Nếu tập tin cũ hơn 6 tháng hoặc có mốc thời gian trong tương lai:
    Sử dụng định dạng `"%b %e  %Y"` (Ví dụ: `Jan  1  2024` - lưu ý có 2 khoảng trắng giữa ngày và năm).
- Chương trình sử dụng `localtime_r()` (phiên bản reentrant thread-safe của `localtime`) và `strftime()` để đảm bảo tính an toàn bộ nhớ và định dạng chuẩn xác.

### 2.7. Nhận diện thiết bị ngoại vi: `major()` và `minor()`
- Với các file thiết bị (`/dev/null`, `/dev/sda`): trường `st_size` không đại diện cho dung lượng bytes mà thay vào đó trường `st_rdev` chứa mã thiết bị phần cứng.
- Chương trình sử dụng các macro chuẩn `<sys/sysmacros.h>`:
  - `major(st_rdev)`: Định danh driver phần cứng (Device Driver ID).
  - `minor(st_rdev)`: Định danh số thứ tự thiết bị cụ thể thuộc driver đó.
  - Khi in ra màn hình, thay vì in dung lượng, chương trình căn lề in theo mẫu: `major, minor` (ví dụ `1, 3`).

---

## 3. GIẢI THÍCH CHUYÊN SÂU: CƠ CHẾ `$PATH` VÀ LÝ DO THỰC THI `./my_ls`

### 3.1. Cơ chế tìm kiếm và phân giải tên lệnh của UNIX Shell
Khi người dùng gõ một lệnh trên Terminal (ví dụ: `ls` hoặc `gcc`), Shell (như Bash, Zsh, Sh) sẽ thực hiện quy trình phân giải theo các bước sau:
1. **Kiểm tra Alias & Function:** Shell xem lệnh có phải alias hoặc hàm nội tại (shell function) không.
2. **Kiểm tra Shell Built-in:** Shell kiểm tra xem lệnh có phải built-in command không (ví dụ: `cd`, `exit`, `echo`).
3. **Phân giải qua biến môi trường `$PATH`:**
   - Nếu chuỗi lệnh **hoàn toàn không chứa ký tự gạch chéo `/`** (đây là một "simple command name"), Shell sẽ đọc giá trị của biến môi trường `$PATH`.
   - Chuỗi `$PATH` gồm danh sách các đường dẫn thư mục được phân tách bởi dấu hai chấm `:`, ví dụ:
     ```bash
     PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
     ```
   - Shell sẽ duyệt tuần tự từ trái qua phải qua từng thư mục trong danh sách này. Tại mỗi thư mục, Shell thực hiện system call `stat()` hoặc `access()` để kiểm tra xem file thực thi có tên trùng khớp có tồn tại và có quyền thực thi (`X_OK`) hay không.
   - Khi gõ lệnh `ls`, Shell tìm thấy file thực thi tại vị trí `/bin/ls` (hoặc `/usr/bin/ls`) và thực hiện system call `fork()` + `execve("/bin/ls", ...)` ngay lập tức.

### 3.2. Triết lý thiết kế An toàn (Security by Design) trong UNIX
- Tại sao thư mục hiện tại `.` (current working directory) **không bao giờ** được đưa vào biến `$PATH` mặc định của người dùng trên bất kỳ bản phân phối UNIX/Linux chuẩn nào?
- Điều này bắt nguồn từ nguyên lý bảo mật cơ bản: **Không tin cậy môi trường không được kiểm soát (Zero-trust environment)**.
- Người dùng thường xuyên di chuyển giữa các thư mục khác nhau (`cd /tmp`, `cd ~/Downloads`, `cd /shared_data`). Các thư mục này có thể bị kiểm soát hoặc ghi đè bởi người dùng khác hoặc các tiến trình không an toàn.

### 3.3. Phân tích hiểm họa bảo mật: PATH Hijacking & Trojan Injection
Nếu thư mục hiện tại `.` nằm trong `$PATH` (ví dụ `PATH=.:/usr/bin:/bin`):
1. **Kịch bản tấn công thư mục công cộng (`/tmp`):**
   - Kẻ tấn công tạo một file thực thi mã độc mang tên `ls` bên trong `/tmp`:
     ```bash
     cat << 'EOF' > /tmp/ls
     #!/bin/sh
     cp /bin/sh /tmp/.backdoor_root && chmod 4755 /tmp/.backdoor_root
     exec /bin/ls "$@"
     EOF
     chmod +x /tmp/ls
     ```
   - Khi Quản trị viên hệ thống (root) vào `/tmp` để kiểm tra sự cố và gõ lệnh `ls`:
     - Do `.` nằm trước `/bin` trong `$PATH`, Shell sẽ ưu tiên chạy file `./ls` của kẻ tấn công thay vì `/bin/ls` của hệ thống.
     - Mã độc được kích hoạt dưới quyền `root`, cài đặt backdoor suid, sau đó vẫn chạy `/bin/ls` thật để đánh lừa quản trị viên rằng không có gì bất thường xảy ra.
   - Đây chính là lỗ hổng kinh điển **PATH Hijacking / Binary Planting**.
2. **Kịch bản Typo-Squatting:**
   - Kẻ tấn công tạo các file mã độc tên `sl`, `claer`, `gerp` trong thư mục dự án chung. Khi một lập trình viên gõ nhầm lệnh, mã độc sẽ lập tức được thực thi.

Do đó, các tiêu chuẩn an toàn của POSIX và UNIX quy định: **Không bao giờ đưa `.` vào `$PATH`**.

### 3.4. Bản chất cú pháp của tiền tố `./`
- Khi người dùng gõ `./my_ls`:
  - Ký tự `.` đại diện cho thư mục làm việc hiện tại (Current Working Directory).
  - Dấu gạch chéo `/` là ký tự phân tách đường dẫn (Pathname separator).
  - Khi Shell phân tích cú pháp thấy **tên lệnh có chứa ít nhất một dấu gạch chéo `/`**, Shell sẽ **hoàn toàn bỏ qua bước tìm kiếm trong biến `$PATH`**.
  - Shell hiểu đây là một đường dẫn tương đối (Relative Pathname) chỉ đích danh vị trí của tập tin nhị phân.
  - Shell gọi trực tiếp system call `execve("./my_ls", argv, envp)`. Nếu file có quyền execute (`chmod +x`), nhân Linux sẽ nạp nhị phân ELF của `my_ls` vào bộ nhớ và tiến hành thực thi.

---

## 4. BẢNG ĐẶC TẢ 19 CỜ LỆNH VÀ QUY TẮC GHI ĐÈ (OVERRIDE RULES)

Dự án cài đặt đầy đủ và chuẩn xác 19 cờ lệnh theo NetBSD 10.1 `ls(1)` manual:

| Cờ lệnh | Tên gọi & Ý nghĩa | Hành vi chi tiết |
|:---:|:---|:---|
| `-A` | Almost all | Liệt kê toàn bộ các file ẩn bắt đầu bằng `.`, ngoại trừ chính xác `.` và `..`. (Mặc định tự động bật với super-user / root). |
| `-a` | All | Liệt kê tất cả các mục trong thư mục, bao gồm cả `.` và `..`. |
| `-c` | Status change time | Sử dụng thời gian thay đổi trạng thái file (`st_ctime`) để sắp xếp (`-t`) hoặc hiển thị (`-l`). |
| `-d` | Directories as files | Coi thư mục như file thường (không duyệt nội dung bên trong). Vô hiệu hóa tính năng đệ quy. |
| `-F` | Classify | Thêm ký tự nhận diện kiểu file: `/` (dir), `*` (executable), `@` (symlink), `=` (socket), `\|` (FIFO). |
| `-f` | Unsorted | Tắt sắp xếp (đọc thứ tự gốc từ disk), đồng thời tự động kích hoạt cờ `-a`. |
| `-h` | Human-readable | Chuyển đổi kích thước file và tổng block sang dạng đọc được (`B`, `K`, `M`, `G`, `T`). Ghi đè `-k`. |
| `-i` | Inode number | In số chỉ mục inode (`st_ino`) của tập tin ở đầu mỗi dòng. |
| `-k` | Kilobytes | Đơn vị kích thước/blocks tính theo Kilobytes (1024 bytes). Ghi đè `-h`. |
| `-l` | Long format | Định dạng chi tiết: mode (10 ký tự), nlink, owner, group, size/device, timestamp, filename -> target. |
| `-n` | Numeric IDs | Giống `-l`, nhưng hiển thị UID và GID dạng số nguyên thay vì tra cứu tên text. |
| `-q` | Non-printable as '?' | Thay thế các ký tự không in được (`!isprint()`) trong tên file bằng dấu `?`. Mặc định khi stdout là terminal. |
| `-R` | Recursive | Duyệt đệ quy toàn bộ các thư mục con cấp dưới gặp phải. |
| `-r` | Reverse sort | Đảo ngược thứ tự sắp xếp (ngược bảng chữ cái, nhỏ nhất trước, hoặc cũ nhất trước). |
| `-S` | Sort by size | Sắp xếp theo dung lượng tập tin giảm dần (file lớn nhất đứng trước). |
| `-s` | Block count | Hiển thị số block đĩa thực tế được cấp phát cho tập tin. In dòng `total` cho thư mục. |
| `-t` | Sort by time | Sắp xếp theo timestamp giảm dần (file mới nhất/thay đổi gần nhất đứng trước). |
| `-u` | Access time | Sử dụng thời gian truy cập file (`st_atime`) để sắp xếp (`-t`) hoặc hiển thị (`-l`). |
| `-w` | Raw non-printable | In nguyên bản các ký tự không in được. Mặc định khi stdout bị pipe hoặc chuyển hướng ra file. |

### Quy tắc ưu tiên và ghi đè (Override Rules) theo chuẩn NetBSD 10.1:
1. **`-w` và `-q`:** Ghi đè lẫn nhau, cờ nào xuất hiện sau cùng trên dòng lệnh sẽ quyết định cách xử lý ký tự không in được.
2. **`-l` và `-n`:** Ghi đè lẫn nhau, cờ xuất hiện sau cùng sẽ quyết định định dạng (hiển thị text name hay numeric id).
3. **`-c` và `-u`:** Ghi đè lẫn nhau, cờ xuất hiện sau cùng sẽ quyết định loại timestamp sử dụng (`ctime` hay `atime`).
4. **`-d` và `-R`:** `-d` luôn ngăn chặn việc duyệt nội dung thư mục con.
5. **`-h` và `-k`:** Ghi đè lẫn nhau, cờ nằm bên phải nhất (rightmost) sẽ quyết định đơn vị kích thước.
6. **`-f`:** Vô hiệu hóa sắp xếp, ghi đè và tự động kích hoạt `-a`.

---

## 5. KẾT QUẢ KIỂM THỬ THỰC NGHIỆM ĐỐI CHIẾU (VERIFICATION RESULTS)

Hệ thống kiểm thử tự động `tests/test_runner.sh` đã thực thi kiểm tra đối chiếu character-by-character giữa `./my_ls` và lệnh `ls` của hệ điều hành với biến chuẩn `POSIXLY_CORRECT=1` qua công cụ `diff -u`.

### Bảng kết quả chạy tự động (43/43 Test Cases Passed - Tỷ lệ Đạt: 100%):

| Nhóm Kiểm thử | STT | Tên Test Case | Cờ Lệnh Kiểm Tra | Kết Quả |
|:---|:---:|:---|:---:|:---:|
| **1. Basic Listing** | 01 | Default listing (no flags) | `(none)` | **PASS** |
| | 02 | All entries | `-a` | **PASS** |
| | 03 | Almost all entries | `-A` | **PASS** |
| **2. Formatting** | 04 | Long format | `-l` | **PASS** |
| | 05 | Numeric IDs long format | `-n` | **PASS** |
| | 06 | Classify indicators | `-F` | **PASS** |
| | 07 | Directory as plain file | `-d .` | **PASS** |
| | 08 | Long format with dir as plain file | `-ld dir_alpha` | **PASS** |
| **3. Sorting** | 09 | Sort by size descending | `-S` | **PASS** |
| | 10 | Sort by time descending | `-t` | **PASS** |
| | 11 | Reverse lexicographical sort | `-r` | **PASS** |
| | 12 | Reverse sort by size | `-rS` | **PASS** |
| | 13 | Reverse sort by time | `-rt` | **PASS** |
| | 14 | Long format sorted by size | `-lS` | **PASS** |
| | 15 | Long format sorted by time | `-lt` | **PASS** |
| **4. Sizes & Blocks** | 16 | Inode display | `-i` | **PASS** |
| | 17 | Block count display | `-s` | **PASS** |
| | 18 | Kilobytes block count | `-sk` | **PASS** |
| | 19 | Human-readable sizes | `-lh` | **PASS** |
| | 20 | Combined inode and blocks | `-is` | **PASS** |
| | 21 | Combined inode and long format | `-li` | **PASS** |
| **5. Time Variants** | 22 | Access time sorting | `-tu` | **PASS** |
| | 23 | Status change time sorting | `-tc` | **PASS** |
| | 24 | Access time long format | `-lu` | **PASS** |
| | 25 | Status change time long format | `-lc` | **PASS** |
| **6. Recursion** | 26 | Recursive listing | `-R` | **PASS** |
| | 27 | Recursive listing with classify | `-RF` | **PASS** |
| | 28 | Recursive listing with subfolder | `-R dir_alpha` | **PASS** |
| **7. Override Rules** | 29 | Override `-k` then `-h` (rightmost wins) | `-kh` | **PASS** |
| | 30 | Override `-h` then `-k` (rightmost wins) | `-hk` | **PASS** |
| | 31 | Override `-l` then `-n` (numeric wins) | `-ln` | **PASS** |
| | 32 | Override `-n` then `-l` (symbolic wins) | `-nl` | **PASS** |
| | 33 | Override `-c` then `-u` (atime wins) | `-tcu` | **PASS** |
| | 34 | Override `-u` then `-c` (ctime wins) | `-tuc` | **PASS** |
| | 35 | Override `-R` then `-d` (`-d` wins) | `-Rd .` | **PASS** |
| | 36 | Override `-d` then `-R` (`-d` wins) | `-dR dir_alpha`| **PASS** |
| **8. Multi-operands** | 37 | Multiple file operands | `file_a file_b` | **PASS** |
| | 38 | File and directory operands | `file_a dir_alpha`| **PASS** |
| | 39 | Multiple directory operands | `dir_alpha dir_beta`| **PASS** |
| | 40 | Multiple operands with long format | `-l file_a dir_alpha`| **PASS** |
| **9. Error Handling** | 41 | Non-existent file operand (exit 1) | `nonexistent_file` | **PASS** |
| | 42 | Non-existent directory operand (exit 1) | `nonexistent_dir/` | **PASS** |
| | 43 | Invalid command-line option (exit 1) | `-Z` | **PASS** |

---

## 6. HƯỚNG DẪN BIÊN DỊCH VÀ SỬ DỤNG

### 6.1. Yêu cầu môi trường
- Hệ điều hành: Linux / WSL2 / FreeBSD / NetBSD / macOS.
- Trình biên dịch: GCC hoặc Clang hỗ trợ chuẩn C11.
- Công cụ: GNU Make.

### 6.2. Các lệnh Make hỗ trợ
1. **Biên dịch tiêu chuẩn:**
   ```bash
   make
   ```
   Tạo ra file thực thi `./my_ls`.

2. **Dọn dẹp môi trường biên dịch:**
   ```bash
   make clean
   ```

3. **Chạy bộ kiểm thử tự động:**
   ```bash
   make test
   ```

4. **Biên dịch chế độ gỡ lỗi (Debug mode):**
   ```bash
   make debug
   ```

### 6.3. Ví dụ sử dụng
```bash
# Liệt kê thư mục hiện tại
./my_ls

# Liệt kê đầy đủ file ẩn dưới dạng chi tiết
./my_ls -la

# Liệt kê đệ quy có phân loại và dung lượng dễ đọc
./my_ls -RFh

# Sắp xếp theo dung lượng giảm dần kèm số inode
./my_ls -liS

# Liệt kê nhiều đối số kết hợp
./my_ls -l include/ src/
```

---
*Báo cáo hoàn thành ngày 11 tháng 10 năm 2026. Mã nguồn dự án được lưu trữ và kiểm soát phiên bản bằng Git.*
