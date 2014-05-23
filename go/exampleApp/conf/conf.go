package conf

import (
  "encoding/json"
  "os"
  )

var Conf *Config

type Config struct {
  ConfFilename              string
  TxProcUdDatagramPath      string
  TxProcUdStreamPath        string
} // type Config

const confPath = "/usr/local/etc/"

func NewConfig( appName string ) (conf *Config, err error) {
  ConfFilename := confPath + appName + ".cnf"
  fh,err := os.Open( ConfFilename )
  if err != nil { return nil,err }

  decoder := json.NewDecoder( fh )
  config := Config{}
  if err = decoder.Decode(&config); err!=nil { return nil,err }
  Conf = &config
  return Conf,nil
} // NewConfig
