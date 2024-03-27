# Zadanie - pracownicy
Program jest systemem dystrybucji zadań dla pracowników z wykorzystaniem kolejek wiadomości POSIX.
System symuluje pracę przez dodawanie dwóch losowych liczb zmiennoprzecinkowych [0-100.0] oraz uśpienie procesu na losowy czas (500ms - 2000ms).
# 
Główny proces (serwer) co losowy czas (t1-t2 ms, 100 <= t1 <= t2 <= 5000, gdzie t1 i t2 to parametry programu) dodaje nowe zadanie do kolejki. Każde zadanie to para liczb zmiennoprzecinkowych. 
Tworzone są procesy dzieci (n pracowników, 2 <= n <= 100, gdzie n - parametr programu), które rejestrują się w kolejce zadań o nazwie "task_queue_{server_pid}".

Pracownicy oczekując na swoje zadania pobierają je z kolejki, gdy są dostępne i nie są zajęci. Każdy pracownik zwraca wyniki przez swoją własną kolejkę o nazwie "result_queue_{server_pid}_{worker_id}"
