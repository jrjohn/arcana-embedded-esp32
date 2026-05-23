// Jenkinsfile — multibranch pipeline for arcana-embedded-esp32 (ESP32 firmware build)
// Adapted from legacy esp32-app-pipeline single-branch job.
//
// Key differences from the legacy XML-embedded script:
//   * `checkout scm` (no hardcoded branch=main)         — supports every branch + every PR
//   * `pollSCM` trigger removed                         — multibranch + GitHub webhook drive triggers
//   * `dir("${env.PROJECTS_DIR}/arcana-embedded-esp32")` removed — multibranch uses workspace root
//   * `Arch Qube Metrics` gated `when { branch 'main' }` — main-only metrics push
//   * SonarQube gets pullrequest.* params on PRs         — PR-decoration in Sonar UI
//   * Post-build messages include branch/PR context

pipeline {
    agent any

    options {
        timeout(time: 30, unit: 'MINUTES')
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

        stage("Pull Build Image") {
            steps { sh "docker pull espressif/idf:v6.0" }
        }

        stage("Build Firmware") {
            steps {
                // Jenkins is running inside a container; the host docker daemon
                // sees a different path for our workspace. Translate container path
                // to host path so the compose `volumes: .:/project` resolves correctly.
                sh '''
                    HOST_WS=$(echo "$WORKSPACE" | sed 's|^/var/jenkins_home/workspace|/data/docker/volumes/devops_jenkins_home/_data/workspace|')
                    echo "Container WORKSPACE=$WORKSPACE"
                    echo "Host PROJECT_PATH=$HOST_WS"
                    PROJECT_PATH="$HOST_WS" docker compose -f docker-compose.ci.yml run --rm esp32-build
                '''
            }
        }

        stage("Test Coverage") {
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'UNSTABLE') {
                    sh '''
                        docker compose -f docker-compose.test.yml build
                        docker compose -f docker-compose.test.yml run --name esp32-test-runner test || true
                        rm -rf ./coverage.xml ./coverage.info
                        docker cp esp32-test-runner:/workspace/coverage.xml ./coverage.xml 2>/dev/null || true
                        docker cp esp32-test-runner:/workspace/coverage.info ./coverage.info 2>/dev/null || true
                        docker rm esp32-test-runner 2>/dev/null || true
                    '''
                }
            }
        }

        stage("SonarQube Analysis") {
            steps {
                catchError(buildResult: 'SUCCESS', stageResult: 'UNSTABLE') {
                    withSonarQubeEnv('SonarQube') {
                        script {
                            def prArgs = env.CHANGE_ID ? """ \
                                -Dsonar.pullrequest.key=${env.CHANGE_ID} \
                                -Dsonar.pullrequest.branch=${env.BRANCH_NAME} \
                                -Dsonar.pullrequest.base=${env.CHANGE_TARGET}""" : ''
                            sh "sonar-scanner -Dsonar.projectKey=esp32-app -Dsonar.scm.disabled=true${prArgs}"
                        }
                    }
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
                catchError(buildResult: 'SUCCESS', stageResult: 'UNSTABLE') {
                    sh """
                        mkdir -p arch-qube-reports
                        docker run --rm \\
                            --network devops_default \\
                            -v \$(pwd):/project \\
                            -v \$(pwd)/arch-qube-reports:/output \\
                            arcana.boo/arcana/arch-qube:latest scan /project \\
                            --framework esp32 --no-ai \\
                            --ci --format json,markdown \\
                            -o /output --threshold 90 || true
                    """
                }
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
