# SomaiyaSat OS mini project - run every command from this folder
#   make        compile the 4 C programs
#   make ai     create synthetic data + train the AI model
#   make run    run all 4 experiments
#   make test   run the failure / invalid-input cases
#   make web    start the website -> open http://localhost:8000
#   make clean  delete compiled programs and generated files

.PHONY: all ai run test web clean

all:
	gcc -Wall -o exp4/mission exp4/mission.c
	gcc -Wall -o exp6/scheduler exp6/scheduler.c
	gcc -Wall -pthread -o exp7/buffer exp7/buffer.c
	gcc -Wall -o exp9/bankers exp9/bankers.c

ai:
	python3 ai/train_model.py

run: all
	@echo "\n########## EXP 4: processes + system calls ##########"
	./exp4/mission
	@echo "\n########## EXP 6: SJF and Round Robin ##########"
	./exp6/scheduler exp6/tasks.txt 4 8 16
	@echo "\n########## EXP 7: producer-consumer ##########"
	./exp7/buffer
	./exp7/buffer unsafe
	@echo "\n########## EXP 9: Banker's algorithm ##########"
	./exp9/bankers

test: all
	@echo "\n--- Test 1: sensor failure -> SAFE MODE (Exp 4) ---"
	./exp4/mission --fail
	@echo "\n--- Test 2: low battery -> safety rule forces beacon (AI) ---"
	python3 ai/generate_pass.py --battery 15 && python3 ai/router.py
	@echo "\n--- Test 3: invalid burst time (Exp 6) ---"
	-./exp6/scheduler exp6/tasks_invalid.txt 4
	@echo "\n--- Test 4: zero time quantum (Exp 6) ---"
	-./exp6/scheduler exp6/tasks.txt 0
	@echo "\n--- Test 5: race condition without mutex (Exp 7) ---"
	./exp7/buffer unsafe
	@echo "\n--- Test 6: unsafe + invalid requests (Exp 9) ---"
	./exp9/bankers | grep -E "REQUEST|GRANT|DENY|WAIT|ERROR"
	@echo "\n(restoring the normal pass)"
	./exp4/mission > /dev/null

web: all
	python3 web/server.py

clean:
	rm -f exp4/mission exp6/scheduler exp7/buffer exp9/bankers
	rm -f data/*.csv data/*.pkl data/*.txt logs/*.txt
