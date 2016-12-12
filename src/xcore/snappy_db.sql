CREATE DATABASE IF NOT EXISTS snappy;
DROP TABLE IF EXISTS snappy.jobs ;


CREATE TABLE snappy.jobs (
    /* main data structure */
    id                      int NOT NULL AUTO_INCREMENT, 
    sub                     int NOT NULL DEFAULT 0,
    next                    int NOT NULL DEFAULT 0,

    /* auxilary info for fast search and update */
    parent                  int NOT NULL DEFAULT 0,  /* aux */ 
    grp                     int NOT NULL DEFAULT 0,  /* aux */
    root                    int NOT NULL DEFAULT 0,  /* aux */


    /* job state machine - 32 bit bitfield  
      
    bit field definition: 
    0: is_new, a newly created job.
    1: is_done, in a finished state, absorbing state
    2: is_ready: the job has been scheduled to run at a certain time in future.
    3: is_running: the job is currently running.
    4: is_blocked: the job is paused 
    5: is_terminiated: terminated but needs resource cleaning, no sub job should be
    created in this state.

    */
    state                   int NOT NULL DEFAULT 1,
    /* for quick search, consistent with state:is_done */
    done                    tinyint(1) NOT NULL DEFAULT 0,

    result                  int DEFAULT 0,

    feid                    varchar(36),       /* main */
    /* 
    state log: json array of log item
       [
        <bool>,             // 0: machine generated, 1: human generated
        <int>,              // old state 
        <int>,              // new state
        <int>,              // who make the change?
        <int>,              // timestamp in unix time
        <int>,              // result code
        <string>,           // result msg
        <string>            // extra info, context, etc
       ]
    */ 
    log         varchar(1024) DEFAULT '',
                                                             
    /* 32 bit mask, defines activated arg for broker processor */
    policy      int DEFAULT 1,
    arg0        varchar(1024) DEFAULT '',  /* reserved for handler */
    arg1        varchar(1024) DEFAULT '',   
    arg2        varchar(1024) DEFAULT '',   
    arg3        varchar(1024) DEFAULT '',   
    arg4        varchar(1024) DEFAULT '',   
    arg5        varchar(1024) DEFAULT '',   
    arg6        varchar(1024) DEFAULT '',   
    arg7        varchar(1024) DEFAULT '', 
    /* 
        required column:
        proc:                   int
    */
    PRIMARY KEY (id),
    KEY (feid),
    KEY (state),
    KEY (done),
    KEY (feid,root)
) ENGINE=InnoDB DEFAULT CHARSET=utf8

