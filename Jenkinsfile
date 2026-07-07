// Jenkinsfile — multibranch pipeline for arcana-embedded-esp32 (ESP32 firmware build)
// Adapted from legacy esp32-app-pipeline single-branch job.
//
// Key differences from the legacy XML-embedded script:
//   * `checkout scm` (no hardcoded branch=main)         — supports every branch + every PR
//   * `pollSCM` trigger removed                         — multibranch + GitHub webhook drive triggers
//   * `dir("${env.PROJECTS_DIR}/arcana-embedded-esp32")` removed — multibranch uses workspace root
//   * `Arch Qube Metrics` gated `when { branch 'main' }` — main-only metrics push
//   * Post-build messages include branch/PR context
//
// Blocking CI gates (hardened 2026-05-28, mirrors arcana-android PR #11 / arcana-cloud-go):
//   * Unit tests are blocking — Dockerfile.test runs ctest WITHOUT `|| true`, so a test
//     failure aborts `docker compose build` and fails the Test Coverage stage.
//   * SonarQube quality gate is POLLED via API (ce/task -> qualitygates/project_status);
//     build fails if gate != OK. No sonar.pullrequest.* params (Community Build rejects them).
//   * Architecture Qube uses docker create + tar | docker cp (NOT `-v $(pwd):/project`, which
//     is empty under DinD because the Jenkins workspace is a named volume) at --threshold 90.

pipeline {
    agent any

    options {
        // Full clean idf.py firmware rebuild alone is ~25-29 min; the now-blocking
        // arch-qube source scan + SonarQube quality-gate poll add a few more, so 30 min
        // was too tight (last build was 28.6 min). Raise headroom to avoid timeout aborts.
        timeout(time: 45, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '10', artifactNumToKeepStr: '1'))
        disableConcurrentBuilds()
        timestamps()
    }

    environment {
        APP_NAME = "esp32-app"
        VERSION  = "1.0.0"
    }

    stages {
        stage("Checkout") {
            steps {
                checkout scm
                sh 'git log -1 --oneline'
                script {
                    echo "Branch: ${env.BRANCH_NAME ?: 'unknown'}"
                    echo "PR: ${env.CHANGE_ID ?: 'no'} (target: ${env.CHANGE_TARGET ?: 'n/a'})"
                }
            }
        }

        stage("Cleanup Old Images") {
            // Disk-hygiene sweep (mirrors arcana-cloud-go). esp32 firmware + test images
            // are heavy multi-stage builds that leave dangling layers; without per-pipeline
            // pruning these pile up on /data and have tripped the Built-In Node disk cutoff
            // + SonarQube ES flood-stage watermark. `|| true` on purpose — cleanup must never
            // fail the build. (esp32 has no registry build- tags, so that branch is a no-op.)
            steps {
                sh '''
                    # Remove dangling/unused images to free disk space
                    docker image prune -f || true
                    # Keep only last 3 build-tagged images for this app (no-op unless tagged)
                    docker images --format '{{.Repository}}:{{.Tag}}' \
                        | grep "${APP_NAME}.*build-" \
                        | sort -t- -k2 -rn \
                        | tail -n +4 \
                        | xargs -r docker rmi 2>/dev/null || true
                    # Stop leftover test containers from prior/aborted builds
                    docker compose -f docker-compose.test.yml down \
                        --remove-orphans 2>/dev/null || true
                '''
            }
        }

        stage("Pull Build Image") {
            // Pre-warm the EXACT image docker-compose.ci.yml builds from (v6.0.1).
            // Pulling the older v6.0 tag here was both wrong (compose uses v6.0.1) and a
            // hang risk: v6.0 is uncached, so build #12 stalled ~45 min on a fresh multi-GB
            // pull and timed out. v6.0.1 is already cached by prior builds -> fast cache hit.
            steps { sh "docker pull espressif/idf:v6.0.1" }
        }

        stage("Build Firmware") {
            steps {
                // Jenkins is running inside a container; the host docker daemon
                // sees a different path for our workspace. Translate container path
                // to host path so the compose `volumes: .:/project` resolves correctly.
                // jenkins_home is a host bind mount at /opt/arcana-state/jenkins-home
                // (see `docker inspect jenkins` .Mounts), NOT the old devops_jenkins_home
                // named-volume path -- that stale prefix silently bind-mounted an empty
                // auto-created directory into /project, so idf.py saw no CMakeLists.txt.
                sh '''
                    HOST_WS=$(echo "$WORKSPACE" | sed 's|^/var/jenkins_home/workspace|/opt/arcana-state/jenkins-home/workspace|')
                    echo "Container WORKSPACE=$WORKSPACE"
                    echo "Host PROJECT_PATH=$HOST_WS"
                    PROJECT_PATH="$HOST_WS" docker compose -f docker-compose.ci.yml run --rm esp32-build
                    # esp32-build runs as root, so build/ lands root-owned on the host
                    # workspace. Jenkins' own (non-root) workspace clean then fails with
                    # "Operation not permitted" on a later build's Checkout SCM. Reset
                    # ownership back to the Jenkins user via a throwaway root container.
                    docker run --rm -v "$HOST_WS":/project espressif/idf:v6.0.1 chown -R "$(id -u):$(id -g)" /project/build || true
                '''
            }
        }

        stage("Test Coverage") {
            steps {
                sh '''
                    set -e
                    # Unit tests run inside `docker compose build` (Dockerfile.test runs ctest
                    # WITHOUT `|| true`), so a failing test aborts the image build and fails
                    # this stage — the unit-test gate is now real (it was swallowed before).
                    docker rm -f esp32-test-runner 2>/dev/null || true
                    docker compose -f docker-compose.test.yml build
                    # `run test` is a no-op echo; its only purpose is to spawn a container we
                    # can docker cp coverage out of (DinD-safe — no host bind mount).
                    docker compose -f docker-compose.test.yml run --name esp32-test-runner test
                    rm -rf ./coverage.xml ./coverage.info
                    docker cp esp32-test-runner:/workspace/coverage.xml ./coverage.xml
                    docker cp esp32-test-runner:/workspace/coverage.info ./coverage.info
                    docker rm -f esp32-test-runner 2>/dev/null || true
                    ls -lh coverage.xml coverage.info
                '''
            }
        }

        stage("SonarQube Analysis") {
            steps {
                withSonarQubeEnv('SonarQube') {
                    sh "sonar-scanner -Dsonar.projectKey=esp32-app -Dsonar.scm.disabled=true"
                    // Poll the SonarQube quality gate via API and fail the build if it is
                    // not OK. Community Build has no webhook waitForQualityGate(), so we read
                    // the ceTaskId from report-task.txt, wait for analysis, then check status.
                    sh '''
                        set -e
                        TOKEN="${SONAR_AUTH_TOKEN:-$SONAR_TOKEN}"
                        RT=.scannerwork/report-task.txt
                        [ -f "$RT" ] || { echo "report-task.txt missing"; exit 1; }
                        CE_TASK_ID=$(grep '^ceTaskId=' "$RT" | cut -d= -f2-)
                        ANALYSIS_ID=""
                        for i in $(seq 1 60); do
                            RESP=$(curl -s -u "$TOKEN:" "$SONAR_HOST_URL/api/ce/task?id=$CE_TASK_ID")
                            ST=$(echo "$RESP" | grep -o '"status":"[A-Z_]*"' | head -1 | cut -d'"' -f4)
                            echo "  CE status: ${ST:-?} (try $i)"
                            if [ "$ST" = "SUCCESS" ]; then ANALYSIS_ID=$(echo "$RESP" | grep -o '"analysisId":"[^"]*"' | head -1 | cut -d'"' -f4); break;
                            elif [ "$ST" = "FAILED" ] || [ "$ST" = "CANCELED" ]; then echo "CE $ST"; exit 1; fi
                            sleep 5
                        done
                        [ -n "$ANALYSIS_ID" ] || { echo "CE timeout"; exit 1; }
                        GATE=$(curl -s -u "$TOKEN:" "$SONAR_HOST_URL/api/qualitygates/project_status?analysisId=$ANALYSIS_ID")
                        GST=$(echo "$GATE" | grep -o '"status":"[A-Z]*"' | head -1 | cut -d'"' -f4)
                        echo "Quality gate: ${GST:-UNKNOWN}"
                        if [ "$GST" != "OK" ]; then echo "$GATE"; exit 1; fi
                    '''
                }
            }
        }

        stage("Extract Artifacts") {
            steps {
                sh "rm -rf /tmp/esp32-app-firmware && mkdir -p /tmp/esp32-app-firmware"
                sh "cp build/mqtt5.bin /tmp/esp32-app-firmware/ || echo no-bin"
                sh "cp build/bootloader/bootloader.bin /tmp/esp32-app-firmware/ || echo no-bootloader"
                sh "cp build/partition_table/partition-table.bin /tmp/esp32-app-firmware/ || echo no-partition"
                sh "ls -la /tmp/esp32-app-firmware/"
            }
        }

        stage("Architecture Qube") {
            steps {
                // Blocking architecture gate at --threshold 90. The old `-v $(pwd):/project`
                // bind mount is empty under DinD (the Jenkins workspace is a named volume the
                // host daemon sees at a different path), so arch-qube scanned nothing. Instead
                // create the container with anonymous volumes and stream the source in via
                // `tar | docker cp`, then copy the report out. `--ci` exits non-zero if < 90.
                sh '''
                    docker rm -f arcana-arch-qube-esp32 2>/dev/null || true
                    docker create --name arcana-arch-qube-esp32 --network devops_default \
                        -v /src -v /output \
                        arcana.boo/arcana/arch-qube:latest \
                        scan /src --framework esp32 --no-ai --ci \
                        --format json,markdown -o /output --threshold 90 || exit 1
                    tar --exclude=./.git --exclude=./arch-qube-reports -C . -cf - . \
                        | docker cp - arcana-arch-qube-esp32:/src || exit 1
                    docker start -a arcana-arch-qube-esp32
                    AQ_RC=$?
                    mkdir -p arch-qube-reports
                    docker cp arcana-arch-qube-esp32:/output/. arch-qube-reports/ 2>/dev/null || true
                    docker rm -f arcana-arch-qube-esp32 2>/dev/null || true
                    exit $AQ_RC
                '''
            }
        }

        stage("Arch Qube Metrics") {
            // Metrics script writes to shared report dir, only run for main.
            when { branch 'main' }
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'SUCCESS') {
                    sh "bash /data/projects/_scripts/arch-qube-metrics.sh \$(pwd) arcana-embedded-esp32 || true"
                }
            }
        }
    }

    post {
        success { echo "Pipeline SUCCESS - embedded-esp32 branch=${env.BRANCH_NAME ?: '?'} pr=${env.CHANGE_ID ?: 'no'}" }
        failure { echo "Pipeline FAILED - branch=${env.BRANCH_NAME ?: '?'} pr=${env.CHANGE_ID ?: 'no'}" }
        always  { echo "Build number ${BUILD_NUMBER} done" }
    }
}
