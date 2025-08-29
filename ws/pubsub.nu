#!/usr/bin/env nu


# Check if the qdisc already exists
let exists = (tc qdisc show dev eth0 | str contains "tbf")

# Add the qdisc only if it doesn't exist
if $exists {
    # print "Traffic control rule already exists."
} else {
    # Emulating a WiFi environment
    print "Adding traffic control rule..."
    # qdisc: Queuing discipline
    # tbf: Token Bucket Filter
    # max average transmission rate: 100 megabits/s = 12.5 MB/s
    # max time a packet can stay before being dropped: 50ms
    tc qdisc add dev eth0 root tbf rate 100mbit burst 200kbit latency 50ms
}

def cleanup [] {
    try {
        pkill rmw_zenohd
        pkill ros
    }
}


# Switch these two RMWs to see the difference
# $env.RMW_IMPLEMENTATION = "rmw_cyclonedds_cpp"
$env.RMW_IMPLEMENTATION = "rmw_zenoh_cpp"
$env.RUST_LOG = "info"
# $env.RUST_LOG = "trace"

let enable_compression = false
let enable_shared_memory = false
let enable_qos = false
let qos_express = false

# TODO: Implement it
# let use_qos = false

const SMALL_PAYLOAD = 64
const LARGE_PAYLOAD = (4 * 1024 * 1024)


def main [--mode: string = "sub"] {
    cleanup
    try {
        if $mode == "pub" {
            run_pub
        } else if $mode == "sub" {
            run_sub
        } else {
            print "mode must be either sub or pub"
            exit
        }
    }
    cleanup
}

let qos_config = [
    # Rule 1 for all messages with a size greater than the threshold
    {
        # Payload_size range for the messages matching this item.
        payload_size: "4096..",

        # (Required) List of message types to apply to.
        messages: ["put"],

        # (Required) Rules to apply
        overwrite: {
            # TODO: blockfirst has been corrected to block_first upstream
            # Block only on the most recent sample
            congestion_control: "blockfirst"
            priority: -1
        }
    }

    # Rule 2 for topic_1 regarding small and emergent messages
    {
        key_exprs: ["**/topic_1/**"]

        # (Required) List of message types to apply to.
        messages: ["put"],

        overwrite: {
            express: $qos_express,
            priority: "data_high"
        }
    }

]

let transport_config = {
    unicast: {
        compression: {
            enabled: $enable_compression
        },
    }
    shared_memory: {
        enabled: $enable_shared_memory
    }
}

let tls_transport_config_server = {
    link: {
        tls: {
            listen_private_key: ./tls/172.28.0.2/key.pem
            listen_certificate: ./tls/172.28.0.2/cert.pem
        }
    }
}

let tls_transport_config_client = {
    link: {
        tls: {
            root_ca_certificate: ./tls/minica.pem
        }
    }
}

let cfg = {
    pub_router: {
        # Connect the pub router to the sub router with TLS
        connect/endpoints: [
            # Encrypted Links: tls or quic
            # tls/172.28.0.2:7447
            quic/172.28.0.2:7447
            # tcp/172.28.0.2:7447
        ]
        listen/endpoints: [
            tcp/localhost:7447
        ]

        qos/network: (if ($enable_qos) { $qos_config } else { [] })

        transport: ($transport_config | merge $tls_transport_config_client)
    }

    pub_node: {
        qos/network: (if ($enable_qos) { $qos_config } else { [] })
        transport: $transport_config
    }

    sub_router: {
        listen/endpoints: [
            tcp/localhost:7447

            # Encrypted Links: tls or quic
            # tls/172.28.0.2:7447
            quic/172.28.0.2:7447
        ]
        transport: ($transport_config | merge $tls_transport_config_server)
    }

    sub_node: {
        transport: $transport_config
    }
}


# Collcet it into ZENOH_CONFIG_OVERRIDE format: "key1=value1;key2=value2;..."
def override_by [key: string] {
    $cfg
        | get $key
        | items {|k, v| $'($k)=($v | to json -r)'}
        | str join ";"
}



def run_pub [] {
    let zenohd = job spawn {
        if not ($env.RMW_IMPLEMENTATION =~ "zenoh") {
            return
        }
        with-env { ZENOH_CONFIG_OVERRIDE: (override_by 'pub_router') } {
            ros2 run rmw_zenoh_cpp rmw_zenohd o+e> _pub-router.log
        }
    }

    with-env { ZENOH_CONFIG_OVERRIDE: (override_by 'pub_node') } {
        (ros2 run --prefix "taskset -c 0,2" demo dual_pubsub
            --mode pub
            --duration 100
            --rate1 100
            --rate2 1
            --payload1 $SMALL_PAYLOAD
            --payload2 $LARGE_PAYLOAD
        )
    }

    job kill $zenohd
}

def run_sub [] {
    let zenohd = job spawn {
        if not ($env.RMW_IMPLEMENTATION =~ "zenoh") {
            return
        }
        with-env { ZENOH_CONFIG_OVERRIDE: (override_by 'sub_router') } {
            ros2 run rmw_zenoh_cpp rmw_zenohd o+e> _sub-router.log
        }
    }

    with-env { ZENOH_CONFIG_OVERRIDE: (override_by 'sub_node') } {
        (ros2 run --prefix "taskset -c 1,3" demo dual_pubsub
            --mode sub
            --duration 0
        )
    }

    job kill $zenohd
}
