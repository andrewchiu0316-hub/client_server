# Client–Server 檔案權限管理系統

以 C 語言實作的 TCP client–server 專案，示範多使用者檔案存取、擁有者／群組權限與多執行緒同步。Client 透過終端機送出指令，Server 負責建立、讀取、寫入檔案與修改權限，並將內容儲存在 `data/`。

## 專案功能

- 使用 IPv4 TCP socket 通訊，Server 監聽 `8888` 埠。
- 每個連線由獨立的 POSIX thread 處理。
- 依擁有者（owner）、群組（group）及其他使用者（other）判斷讀寫權限。
- 使用 mutex 保護部分檔案表操作，使用每個檔案的讀寫鎖協調內容存取。
- 支援建立、讀取、覆寫、附加及權限變更。
- Server 在載入檔案及部分操作後，於終端機印出檔案權限清單。

## 檔案結構

```text
client_server/
├── Client.c             # 終端機用戶端與通訊流程
├── Server.c             # 伺服器、權限判斷與檔案管理
├── Makefile             # 原始編譯規則（檔名大小寫注意事項見下方）
├── data/                # 檔案內容與部分 .meta 中繼資料
└── .gitignore           # 排除編譯產物、本機設定及超大測試檔
```

## 編譯與啟動

建議使用 Linux 或 Windows 的 WSL，並安裝 GCC。程式使用 POSIX socket、pthread、`unistd.h` 與 `getline` 等介面，無法直接以一般原生 Windows C 環境編譯。

```bash
git clone https://github.com/andrewchiu0316-hub/client_server.git
cd client_server

gcc Server.c -o server -pthread -Wall
gcc Client.c -o client -pthread -Wall
```

目前 Makefile 引用小寫的 `server.c` 與 `client.c`，但實際檔名為 `Server.c` 與 `Client.c`。在大小寫敏感的系統上，請使用上方指令；若要使用 `make`，需先修正 Makefile 中的來源檔名。

在第一個終端機執行：

```bash
./server
```

在另一個終端機執行：

```bash
./client 127.0.0.1
```

跨電腦連線時，將 `127.0.0.1` 換成 Server 的 IPv4 位址，並確保 TCP `8888` 可連線。Server 使用目前工作目錄下的 `data/`，請從專案根目錄啟動。

## 使用者與群組

Client 啟動後會提示輸入使用者名稱，名稱區分大小寫。使用者清單目前寫在 `Server.c` 中。

| 群組 | 使用者 |
| --- | --- |
| AOS | Ken、Barbie、Alex |
| CSE | John、Mary、Tom |

未知使用者會被拒絕連線。目前僅以輸入的名稱辨識身分，沒有密碼驗證。

## 權限格式

權限字串共有六個位置，依序表示 owner 讀／寫、group 讀／寫、other 讀／寫。範例使用 `r` 表示讀取、`w` 表示寫入、`-` 表示不允許。

| 權限 | 擁有者 | 同群組 | 其他使用者 |
| --- | --- | --- | --- |
| `rwrwrw` | 讀寫 | 讀寫 | 讀寫 |
| `rwr---` | 讀寫 | 唯讀 | 無 |
| `rw----` | 讀寫 | 無 | 無 |

權限判斷先檢查擁有者，再檢查群組，最後檢查其他使用者。只有擁有者可修改權限。程式目前未嚴格驗證權限字串格式，請依上述六位格式輸入。

## 操作指令

| 指令 | 說明 | 範例 |
| --- | --- | --- |
| `new <filename> <permission>` | 建立檔案，登入者成為擁有者 | `new demo.txt rwr---` |
| `read <filename>` | 將檔案內容顯示在 Client 終端機 | `read demo.txt` |
| `write <filename> o` | 覆寫內容 | `write demo.txt o` |
| `write <filename> a` | 附加內容 | `write demo.txt a` |
| `change <filename> <permission>` | 擁有者修改權限 | `change demo.txt rw----` |
| `exit` | 關閉 Client | `exit` |

寫入操作收到 Server 許可後，可輸入多行文字。在 Linux／WSL 終端機中，輸入完畢後於空行按 **Ctrl+D**，Client 才會送出內容。

例如，以 `Ken` 登入後：

```text
new demo.txt rwr---
write demo.txt o
Hello from Ken!
```

接著於空行按 Ctrl+D，等待 `Write OK`。目前 Client 在讀取寫入內容後未清除標準輸入的 EOF 狀態，因此完成寫入後可能直接結束；若要繼續操作，請重新執行 Client 並登入，再輸入：

```text
read demo.txt
change demo.txt rw----
exit
```

請使用不含空白、長度適中的單純檔名，例如 `demo.txt`。

## 資料儲存

- 檔案內容寫入 `data/<filename>`。
- 建立、寫入或修改權限時，Server 會將 owner、group 與六位數字權限寫入 `data/<filename>.meta`。
- **目前重啟時只載入檔案內容，不會還原 `.meta` 的權限資料。** 載入的檔案會被設為 owner `Ken`、group `AOS`，且六項權限全部開啟。
- 約 1 GB 的本機測試檔 `data/test.c` 及其 `.meta` 已排除於版本控制之外，不包含在 GitHub 儲存庫中。

## 目前限制

本專案適合在可信任的本機或隔離測試環境中學習 socket、權限模型與執行緒同步。現有實作仍有下列限制：

- 沒有密碼驗證、傳輸加密或檔案路徑隔離。
- 部分指令解析、檔名長度、檔案數量及 socket 呼叫缺乏完整驗證與錯誤處理。
- TCP 訊息邊界與部分傳送／接收尚未完整處理，檔案大小直接以主機原生 `int` 傳輸，跨平台相容性有限。
- 部分共享狀態的同步仍不完整；讀寫鎖不代表所有多執行緒操作都已安全。
- 權限中繼資料的重啟還原，以及寫入後的 Client EOF 處理，尚待改善。
